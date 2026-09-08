#include "Models/Formats/FbxSkin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Models::FbxInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

std::vector<double> numericArray(const FbxDocument::Node *node)
{
    return node ? node->numericArray() : std::vector<double>{};
}

std::vector<std::int32_t> int32Array(const FbxDocument::Node *node)
{
    if (!node || node->properties.empty()) return {};
    if (const auto *values=node->properties[0].asInt32Array()) return *values;
    if (const auto *values64=node->properties[0].asInt64Array()) {
        std::vector<std::int32_t> result;
        result.reserve(values64->size());
        for (std::int64_t value:*values64) {
            if (value<std::numeric_limits<std::int32_t>::min() || value>std::numeric_limits<std::int32_t>::max()) return {};
            result.push_back(static_cast<std::int32_t>(value));
        }
        return result;
    }
    const auto values=node->numericArray();
    std::vector<std::int32_t> result;
    result.reserve(values.size());
    for (double value:values) result.push_back(static_cast<std::int32_t>(value));
    return result;
}

std::vector<ObjectId> clustersForSkin(const Scene& scene,ObjectId skin)
{
    std::vector<ObjectId> result;
    for (ObjectId id:connectedObjects(scene,skin,"Deformer")) {
        const Object *value=object(scene,id);
        if (value && value->subtype=="Cluster") result.push_back(id);
    }
    return result;
}

ObjectId boneForCluster(const Scene& scene,ObjectId cluster,std::string *error)
{
    const auto models=connectedObjects(scene,cluster,"Model");
    if (models.empty()) {
        fail(error,"FBX Cluster id "+std::to_string(cluster)+" has no linked bone Model");
        return 0;
    }
    if (models.size()>1u) {
        fail(error,"unsupported associate-model bind mode on FBX Cluster id "+std::to_string(cluster));
        return 0;
    }
    return models.front();
}

void addBoneWithParents(const Scene& scene,ObjectId model,SkeletonBuild *build)
{
    if (!build || model==0 || build->indices.find(model)!=build->indices.end()) return;
    const Object *bone=object(scene,model);
    if (!bone || bone->kind!="Model") return;
    const ObjectId parent=parentModel(scene,model);
    if (parent!=0) addBoneWithParents(scene,parent,build);
    if (build->models.size()>=static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) return;
    const std::uint16_t index=static_cast<std::uint16_t>(build->models.size());
    build->models.push_back(model);
    build->indices.emplace(model,index);
}

} // namespace

ObjectId skinForGeometry(const Scene& scene,ObjectId geometry)
{
    for (ObjectId id:connectedObjects(scene,geometry,"Deformer")) {
        const Object *value=object(scene,id);
        if (value && value->subtype=="Skin") return id;
    }
    return 0;
}

SkeletonBuild collectSkeleton(const Scene& scene)
{
    SkeletonBuild result;
    for (ObjectId id:scene.object_order) {
        const Object *geometry=object(scene,id);
        if (!geometry || geometry->kind!="Geometry" || geometry->subtype!="Mesh") continue;
        const ObjectId skin=skinForGeometry(scene,id);
        if (skin==0) continue;
        for (ObjectId cluster:clustersForSkin(scene,skin)) {
            std::string ignored;
            const ObjectId bone=boneForCluster(scene,cluster,&ignored);
            if (bone!=0) addBoneWithParents(scene,bone,&result);
        }
    }
    return result;
}

bool makeSkeleton(
    const Scene& scene,
    const SkeletonBuild& build,
    const std::string& name,
    Animation::Skeleton *out,
    std::string *error)
{
    if (!out) return fail(error,"null FBX skeleton destination");
    Animation::Skeleton result;
    result.name=name;
    result.bones.reserve(build.models.size());

    std::unordered_map<ObjectId,Animation::Mat4> globals;
    for (ObjectId id:build.models) {
        Animation::Mat4 global;
        if (!globalModelMatrix(scene,id,&global,error)) return false;
        globals.emplace(id,global);
    }

    for (ObjectId id:build.models) {
        const Object *model=object(scene,id);
        if (!model) return fail(error,"FBX skeleton references missing Model id "+std::to_string(id));
        Animation::Bone bone;
        bone.name=model->name;
        const ObjectId parent=parentModel(scene,id);
        const auto parent_index=build.indices.find(parent);
        Animation::Mat4 local=globals.at(id);
        if (parent_index!=build.indices.end()) {
            bone.parent=static_cast<std::int32_t>(parent_index->second);
            Animation::Mat4 inverse_parent;
            if (!invertMatrix(globals.at(parent),&inverse_parent)) {
                return fail(error,"singular FBX parent bind transform for bone `"+model->name+"`");
            }
            local=Animation::multiply(inverse_parent,local);
        }
        if (!decomposeTrs(local,&bone.bind_local,error,"bone `"+model->name+"` bind transform")) return false;
        if (!invertMatrix(globals.at(id),&bone.inverse_bind)) {
            return fail(error,"singular FBX global bind transform for bone `"+model->name+"`");
        }
        result.bones.push_back(std::move(bone));
    }
    *out=std::move(result);
    return true;
}

bool buildControlWeights(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton,
    std::size_t control_count,
    std::vector<Animation::SkinWeights> *out,
    std::string *error)
{
    if (!out) return fail(error,"null FBX skin weight destination");
    out->assign(control_count,{});
    struct Influence { float weight; std::uint16_t joint; };
    std::vector<std::vector<Influence>> influences(control_count);

    for (ObjectId cluster_id:clustersForSkin(scene,skin)) {
        const Object *cluster=object(scene,cluster_id);
        if (!cluster) return fail(error,"FBX Skin references missing Cluster id "+std::to_string(cluster_id));
        const ObjectId bone_id=boneForCluster(scene,cluster_id,error);
        if (bone_id==0) return false;
        const auto bone=skeleton.indices.find(bone_id);
        if (bone==skeleton.indices.end()) return fail(error,"FBX Cluster bone is not present in collected skeleton");
        const auto indexes=int32Array(child(cluster->node,"Indexes"));
        const auto weights=numericArray(child(cluster->node,"Weights"));
        if (indexes.size()!=weights.size()) {
            return fail(error,"FBX Cluster `"+cluster->name+"` has mismatched Indexes/Weights arrays");
        }
        for (std::size_t i=0;i<indexes.size();++i) {
            if (indexes[i]<0 || static_cast<std::size_t>(indexes[i])>=control_count) {
                return fail(error,"invalid skin cluster control-point index in Deformer `"+cluster->name+"`");
            }
            if (!std::isfinite(weights[i]) || weights[i]<0.0) {
                return fail(error,"invalid skin cluster weight in Deformer `"+cluster->name+"`");
            }
            if (weights[i]==0.0) continue;
            influences[static_cast<std::size_t>(indexes[i])].push_back({static_cast<float>(weights[i]),bone->second});
        }
    }

    for (std::size_t control=0;control<influences.size();++control) {
        auto& values=influences[control];
        std::sort(values.begin(),values.end(),[](const Influence& a,const Influence& b){return a.weight>b.weight;});
        const std::size_t count=std::min<std::size_t>(values.size(),4u);
        float total=0.0f;
        for (std::size_t i=0;i<count;++i) total+=values[i].weight;
        if (total<=1.0e-8f) continue;
        for (std::size_t i=0;i<count;++i) {
            (*out)[control].joints[i]=values[i].joint;
            (*out)[control].weights[i]=values[i].weight/total;
        }
    }
    return true;
}

bool buildMeshBindPalette(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton,
    std::vector<Animation::Mat4> *out,
    std::string *error)
{
    if (!out) return fail(error,"null FBX mesh bind palette destination");
    out->assign(skeleton.models.size(),Animation::Mat4{});
    for (ObjectId cluster_id:clustersForSkin(scene,skin)) {
        const Object *cluster=object(scene,cluster_id);
        if (!cluster) return fail(error,"FBX Skin references missing Cluster id "+std::to_string(cluster_id));
        const ObjectId bone_id=boneForCluster(scene,cluster_id,error);
        if (bone_id==0) return false;
        const auto bone=skeleton.indices.find(bone_id);
        if (bone==skeleton.indices.end()) return fail(error,"FBX Cluster bone is absent from skeleton");

        const auto transform_values=numericArray(child(cluster->node,"Transform"));
        const auto link_values=numericArray(child(cluster->node,"TransformLink"));
        if (transform_values.size()<16u || link_values.size()<16u) {
            return fail(error,"FBX Cluster `"+cluster->name+"` is missing Transform or TransformLink matrix");
        }
        const Animation::Mat4 transform=canonicalMatrix(scene.basis,matrixFromFbxArray(transform_values));
        const Animation::Mat4 link=canonicalMatrix(scene.basis,matrixFromFbxArray(link_values));
        Animation::Mat4 inverse_link;
        if (!invertMatrix(link,&inverse_link)) return fail(error,"singular TransformLink in FBX Cluster `"+cluster->name+"`");
        (*out)[bone->second]=Animation::multiply(inverse_link,transform);
    }
    return true;
}

} // namespace Models::FbxInternal
