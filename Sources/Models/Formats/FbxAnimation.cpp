#include "Models/Formats/FbxAnimation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Models::FbxInternal {
namespace {

constexpr double kFbxTicksPerSecond = 46186158000.0;
constexpr float kTargetSampleRate = 30.0f;
constexpr std::size_t kMaximumClipSamples = 18000u;

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

std::vector<std::int64_t> int64Array(const FbxDocument::Node *node)
{
    if (!node || node->properties.empty()) return {};
    if (const auto *values=node->properties[0].asInt64Array()) return *values;
    if (const auto *values32=node->properties[0].asInt32Array()) {
        return std::vector<std::int64_t>(values32->begin(),values32->end());
    }
    const auto values=node->numericArray();
    std::vector<std::int64_t> result;
    result.reserve(values.size());
    for (double value:values) result.push_back(static_cast<std::int64_t>(value));
    return result;
}

struct Curve {
    std::vector<std::int64_t> times;
    std::vector<double> values;
};

Curve parseCurve(const Object& curve)
{
    Curve result;
    result.times=int64Array(child(curve.node,"KeyTime"));
    const auto *float_values=child(curve.node,"KeyValueFloat");
    const auto *double_values=child(curve.node,"KeyValueDouble");
    result.values=(float_values?float_values:double_values) ? (float_values?float_values:double_values)->numericArray() : std::vector<double>{};
    const std::size_t count=std::min(result.times.size(),result.values.size());
    result.times.resize(count);
    result.values.resize(count);
    return result;
}

double sampleCurve(const Curve& curve,std::int64_t time,double fallback)
{
    if (curve.times.empty()) return fallback;
    if (time<=curve.times.front()) return curve.values.front();
    if (time>=curve.times.back()) return curve.values.back();
    const auto upper=std::upper_bound(curve.times.begin(),curve.times.end(),time);
    const std::size_t second=static_cast<std::size_t>(upper-curve.times.begin());
    const std::size_t first=second-1u;
    const double span=static_cast<double>(curve.times[second]-curve.times[first]);
    const double factor=span>0.0?static_cast<double>(time-curve.times[first])/span:0.0;
    return curve.values[first]+(curve.values[second]-curve.values[first])*factor;
}

struct NodeCurves {
    ObjectId model=0;
    std::string property;
    std::array<Curve,3> axes;
    std::array<bool,3> present{};
};

bool touchesLayer(const Scene& scene,ObjectId curve_node,const std::unordered_set<ObjectId>& layers)
{
    auto inspect=[&](const std::vector<std::size_t>& indexes,bool incoming) {
        for (std::size_t index:indexes) {
            const Connection& connection=scene.connections[index];
            const ObjectId candidate=incoming?connection.source:connection.destination;
            if (layers.find(candidate)!=layers.end()) return true;
        }
        return false;
    };
    if (const auto found=scene.incoming.find(curve_node);found!=scene.incoming.end() && inspect(found->second,true)) return true;
    if (const auto found=scene.outgoing.find(curve_node);found!=scene.outgoing.end() && inspect(found->second,false)) return true;
    return false;
}

bool curveNodeTarget(const Scene& scene,ObjectId node,ObjectId *model,std::string *property)
{
    auto inspect=[&](const std::vector<std::size_t>& indexes,bool incoming) {
        for (std::size_t index:indexes) {
            const Connection& connection=scene.connections[index];
            if (connection.type!="OP") continue;
            const ObjectId candidate=incoming?connection.source:connection.destination;
            const Object *value=object(scene,candidate);
            if (!value || value->kind!="Model") continue;
            *model=candidate;
            *property=connection.property;
            return true;
        }
        return false;
    };
    if (const auto found=scene.outgoing.find(node);found!=scene.outgoing.end() && inspect(found->second,false)) return true;
    if (const auto found=scene.incoming.find(node);found!=scene.incoming.end() && inspect(found->second,true)) return true;
    return false;
}

void collectCurveAxes(const Scene& scene,ObjectId node,NodeCurves *entry)
{
    auto inspect=[&](const std::vector<std::size_t>& indexes,bool incoming) {
        for (std::size_t index:indexes) {
            const Connection& connection=scene.connections[index];
            if (connection.type!="OP") continue;
            const ObjectId candidate=incoming?connection.source:connection.destination;
            const Object *curve=object(scene,candidate);
            if (!curve || curve->kind!="AnimationCurve") continue;
            int axis=-1;
            if (connection.property.find('X')!=std::string::npos) axis=0;
            else if (connection.property.find('Y')!=std::string::npos) axis=1;
            else if (connection.property.find('Z')!=std::string::npos) axis=2;
            if (axis<0) continue;
            entry->axes[static_cast<std::size_t>(axis)]=parseCurve(*curve);
            entry->present[static_cast<std::size_t>(axis)]=true;
        }
    };
    if (const auto found=scene.incoming.find(node);found!=scene.incoming.end()) inspect(found->second,true);
    if (const auto found=scene.outgoing.find(node);found!=scene.outgoing.end()) inspect(found->second,false);
}

bool setAnimatedProperty(
    TransformProperties *properties,
    const NodeCurves& curves,
    std::int64_t time,
    std::string *error)
{
    Animation::Vec3 *target=nullptr;
    if (curves.property.find("Translation")!=std::string::npos) target=&properties->translation;
    else if (curves.property.find("Rotation")!=std::string::npos) target=&properties->rotation_degrees;
    else if (curves.property.find("Scaling")!=std::string::npos) target=&properties->scale;
    else return fail(error,"unsupported animated FBX property `"+curves.property+"`");

    float *components[3]={&target->x,&target->y,&target->z};
    for (std::size_t axis=0;axis<3u;++axis) {
        if (!curves.present[axis]) continue;
        *components[axis]=static_cast<float>(sampleCurve(curves.axes[axis],time,*components[axis]));
    }
    return true;
}

} // namespace

bool makeAnimations(
    const Scene& scene,
    const SkeletonBuild& skeleton,
    std::vector<Animation::AnimationClip> *out,
    std::string *error)
{
    if (!out) return fail(error,"null FBX animation destination");
    out->clear();
    if (skeleton.models.empty()) return true;

    std::unordered_map<ObjectId,TransformProperties> bind_properties;
    for (ObjectId id:skeleton.models) {
        const Object *model=object(scene,id);
        if (!model) return fail(error,"FBX animation skeleton references missing Model id "+std::to_string(id));
        TransformProperties properties;
        if (!readTransformProperties(*model,&properties,error)) return false;
        bind_properties.emplace(id,properties);
    }

    for (ObjectId stack_id:scene.object_order) {
        const Object *stack=object(scene,stack_id);
        if (!stack || stack->kind!="AnimationStack") continue;
        const auto connected_layers=connectedObjects(scene,stack_id,"AnimationLayer");
        std::unordered_set<ObjectId> layers(connected_layers.begin(),connected_layers.end());
        if (layers.empty()) continue;

        std::vector<NodeCurves> nodes;
        std::int64_t minimum_time=std::numeric_limits<std::int64_t>::max();
        std::int64_t maximum_time=std::numeric_limits<std::int64_t>::min();
        for (ObjectId id:scene.object_order) {
            const Object *curve_node=object(scene,id);
            if (!curve_node || curve_node->kind!="AnimationCurveNode" || !touchesLayer(scene,id,layers)) continue;
            NodeCurves entry;
            if (!curveNodeTarget(scene,id,&entry.model,&entry.property)) continue;
            if (skeleton.indices.find(entry.model)==skeleton.indices.end()) continue;
            collectCurveAxes(scene,id,&entry);
            for (std::size_t axis=0;axis<3u;++axis) {
                if (!entry.present[axis] || entry.axes[axis].times.empty()) continue;
                minimum_time=std::min(minimum_time,entry.axes[axis].times.front());
                maximum_time=std::max(maximum_time,entry.axes[axis].times.back());
            }
            nodes.push_back(std::move(entry));
        }

        const auto local_start=propertyValues(*stack,"LocalStart");
        const auto local_stop=propertyValues(*stack,"LocalStop");
        if (!local_start.empty()) minimum_time=static_cast<std::int64_t>(local_start.front());
        if (!local_stop.empty()) maximum_time=static_cast<std::int64_t>(local_stop.front());
        if (minimum_time==std::numeric_limits<std::int64_t>::max()) minimum_time=0;
        if (maximum_time<minimum_time) maximum_time=minimum_time;

        const double duration=static_cast<double>(maximum_time-minimum_time)/kFbxTicksPerSecond;
        std::size_t sample_count=duration>0.0
            ? static_cast<std::size_t>(std::ceil(duration*kTargetSampleRate))+1u
            : 1u;
        sample_count=std::clamp<std::size_t>(sample_count,1u,kMaximumClipSamples);

        Animation::AnimationClip clip;
        clip.name=stack->name.empty()?"Animation":stack->name;
        clip.duration=static_cast<float>(duration);
        clip.sample_rate=duration>0.0 && sample_count>1u
            ? static_cast<float>(static_cast<double>(sample_count-1u)/duration)
            : kTargetSampleRate;
        clip.tracks.resize(skeleton.models.size());
        for (auto& track:clip.tracks) track.samples.reserve(sample_count);

        for (std::size_t sample=0;sample<sample_count;++sample) {
            const double factor=sample_count>1u?static_cast<double>(sample)/static_cast<double>(sample_count-1u):0.0;
            const std::int64_t time=minimum_time+static_cast<std::int64_t>(static_cast<double>(maximum_time-minimum_time)*factor);
            auto overrides=bind_properties;
            for (const NodeCurves& curves:nodes) {
                auto found=overrides.find(curves.model);
                if (found==overrides.end()) continue;
                if (!setAnimatedProperty(&found->second,curves,time,error)) return false;
            }

            std::unordered_map<ObjectId,Animation::Mat4> globals;
            for (ObjectId id:skeleton.models) {
                Animation::Mat4 global;
                if (!globalModelMatrix(scene,id,overrides,&global,error)) return false;
                globals.emplace(id,global);
            }
            for (std::size_t bone=0;bone<skeleton.models.size();++bone) {
                const ObjectId id=skeleton.models[bone];
                Animation::Mat4 local=globals.at(id);
                const ObjectId parent=parentModel(scene,id);
                if (skeleton.indices.find(parent)!=skeleton.indices.end()) {
                    Animation::Mat4 inverse_parent;
                    if (!invertMatrix(globals.at(parent),&inverse_parent)) {
                        const Object *model=object(scene,id);
                        return fail(error,"singular animated parent transform for FBX bone `"+(model?model->name:std::to_string(id))+"`");
                    }
                    local=Animation::multiply(inverse_parent,local);
                }
                Animation::Transform transform;
                const Object *model=object(scene,id);
                if (!decomposeTrs(local,&transform,error,"animated bone `"+(model?model->name:std::to_string(id))+"`")) return false;
                clip.tracks[bone].samples.push_back(transform);
            }
        }
        out->push_back(std::move(clip));
    }
    return true;
}

} // namespace Models::FbxInternal
