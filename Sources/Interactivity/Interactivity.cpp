#include "Interactivity/Interactivity.hpp"
#include "Interactivity/Math.hpp"

#include "Models/Formats/GltfJson.hpp"
#include "Models/Models.hpp"
#include "Models/Runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Interactivity {
namespace {

namespace Json = Models::Formats::GltfJson;

struct Value {
    std::string type = "float";
    std::vector<double> data {0.0};
};

struct FlowTarget {
    std::size_t node = SIZE_MAX;
    std::string socket = "in";
};

struct Declaration {
    std::string op;
    std::string extension;
};

struct VariableState {
    std::string name;
    Value value;
};

struct EventDefinition {
    std::string id;
    std::unordered_map<std::string, std::size_t> values;
};

struct DelayState {
    std::uint64_t id = 0u;
    double remaining = 0.0;
    FlowTarget target;
};

struct InterpolationState {
    enum class Kind : std::uint8_t { Variable, Pointer };
    Kind kind = Kind::Variable;
    std::size_t variable = SIZE_MAX;
    std::string pointer;
    Value from;
    Value to;
    double elapsed = 0.0;
    double duration = 0.0;
    std::string easing = "linear";
    FlowTarget done;
};

struct AnimationState {
    std::uint64_t id = 0u;
    std::size_t animation = SIZE_MAX;
    double time = 0.0;
    double speed = 1.0;
    double start = 0.0;
    double end = std::numeric_limits<double>::infinity();
    bool loop = false;
    bool paused = false;
};

bool fail(std::string *error, std::string message)
{
    if (error) *error = std::move(message);
    return false;
}

std::vector<std::uint16_t> utf16Units(std::string_view value)
{
    std::vector<std::uint16_t> units;
    units.reserve(value.size());
    for (std::size_t index = 0u; index < value.size();) {
        const auto byte = static_cast<unsigned char>(value[index]);
        std::uint32_t codepoint = 0xfffdu;
        std::size_t length = 1u;
        if (byte < 0x80u) {
            codepoint = byte;
        } else if ((byte & 0xe0u) == 0xc0u && index + 1u < value.size()) {
            const auto b1 = static_cast<unsigned char>(value[index + 1u]);
            if ((b1 & 0xc0u) == 0x80u) {
                codepoint = ((byte & 0x1fu) << 6u) | (b1 & 0x3fu);
                if (codepoint >= 0x80u) length = 2u;
                else codepoint = 0xfffdu;
            }
        } else if ((byte & 0xf0u) == 0xe0u && index + 2u < value.size()) {
            const auto b1 = static_cast<unsigned char>(value[index + 1u]);
            const auto b2 = static_cast<unsigned char>(value[index + 2u]);
            if ((b1 & 0xc0u) == 0x80u && (b2 & 0xc0u) == 0x80u) {
                codepoint = ((byte & 0x0fu) << 12u) | ((b1 & 0x3fu) << 6u) | (b2 & 0x3fu);
                if (codepoint >= 0x800u && !(codepoint >= 0xd800u && codepoint <= 0xdfffu)) length = 3u;
                else codepoint = 0xfffdu;
            }
        } else if ((byte & 0xf8u) == 0xf0u && index + 3u < value.size()) {
            const auto b1 = static_cast<unsigned char>(value[index + 1u]);
            const auto b2 = static_cast<unsigned char>(value[index + 2u]);
            const auto b3 = static_cast<unsigned char>(value[index + 3u]);
            if ((b1 & 0xc0u) == 0x80u && (b2 & 0xc0u) == 0x80u && (b3 & 0xc0u) == 0x80u) {
                codepoint = ((byte & 0x07u) << 18u) | ((b1 & 0x3fu) << 12u) | ((b2 & 0x3fu) << 6u) | (b3 & 0x3fu);
                if (codepoint >= 0x10000u && codepoint <= 0x10ffffu) length = 4u;
                else codepoint = 0xfffdu;
            }
        }
        index += length;
        if (codepoint <= 0xffffu) {
            units.push_back(static_cast<std::uint16_t>(codepoint));
        } else {
            codepoint -= 0x10000u;
            units.push_back(static_cast<std::uint16_t>(0xd800u + (codepoint >> 10u)));
            units.push_back(static_cast<std::uint16_t>(0xdc00u + (codepoint & 0x3ffu)));
        }
    }
    return units;
}

bool socketIdLess(std::string_view a, std::string_view b)
{
    const std::vector<std::uint16_t> aa = utf16Units(a);
    const std::vector<std::uint16_t> bb = utf16Units(b);
    return std::lexicographical_compare(aa.begin(), aa.end(), bb.begin(), bb.end());
}

std::size_t componentCount(std::string_view type)
{
    if (type == "float2") return 2u;
    if (type == "float3") return 3u;
    if (type == "float4") return 4u;
    if (type == "float2x2") return 4u;
    if (type == "float3x3") return 9u;
    if (type == "float4x4") return 16u;
    return 1u;
}

Value defaultValue(std::string type)
{
    Value result;
    result.type = std::move(type);
    result.data.assign(componentCount(result.type), 0.0);
    return result;
}

bool truthy(const Value& value)
{
    return !value.data.empty() && value.data[0] != 0.0 && !std::isnan(value.data[0]);
}

std::int32_t integer(const Value& value)
{
    if (value.data.empty() || !std::isfinite(value.data[0])) return 0;
    const double v = std::trunc(value.data[0]);
    const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::int64_t>(v));
    return std::bit_cast<std::int32_t>(bits);
}

Value boolean(bool value)
{
    return {"bool", {value ? 1.0 : 0.0}};
}

Value integerValue(std::int32_t value)
{
    return {"int", {static_cast<double>(value)}};
}

Value scalar(double value)
{
    return {"float", {value}};
}

Value vectorValue(std::string type, std::initializer_list<double> values)
{
    return {std::move(type), std::vector<double>(values)};
}

Value fromJson(std::string type, const Json::Value *source)
{
    Value result = defaultValue(type);
    if (!source) return result;
    if (source->is(Json::Type::Array)) {
        result.data.clear();
        result.data.reserve(std::max(componentCount(type), source->array.size()));
        for (const Json::Value& item : source->array) {
            if (item.is(Json::Type::Boolean)) result.data.push_back(item.boolean ? 1.0 : 0.0);
            else if (item.is(Json::Type::Number)) result.data.push_back(item.number);
            else result.data.push_back(0.0);
        }
        result.data.resize(componentCount(type), 0.0);
    } else if (source->is(Json::Type::Number)) {
        result.data.assign(componentCount(type), source->number);
    } else if (source->is(Json::Type::Boolean)) {
        result.data.assign(componentCount(type), source->boolean ? 1.0 : 0.0);
    }
    return result;
}

Value broadcast(const Value& value, std::size_t count)
{
    Value result = value;
    if (result.data.empty()) result.data.push_back(0.0);
    if (result.data.size() == 1u && count > 1u) result.data.resize(count, result.data.front());
    else result.data.resize(count, 0.0);
    return result;
}

Value componentwise(const Value& a, const Value& b, const auto& operation)
{
    const std::size_t count = std::max(a.data.size(), b.data.size());
    const Value aa = broadcast(a, count);
    const Value bb = broadcast(b, count);
    Value result = aa;
    result.type = a.data.size() >= b.data.size() ? a.type : b.type;
    for (std::size_t i = 0u; i < count; ++i) result.data[i] = operation(aa.data[i], bb.data[i]);
    return result;
}

Value unary(const Value& a, const auto& operation)
{
    Value result = a;
    for (double& item : result.data) item = operation(item);
    return result;
}

double dot(const Value& a, const Value& b)
{
    const std::size_t count = std::min(a.data.size(), b.data.size());
    double result = 0.0;
    for (std::size_t i = 0u; i < count; ++i) result += a.data[i] * b.data[i];
    return result;
}

Value normalized(const Value& input, bool *valid = nullptr)
{
    const double length2 = dot(input, input);
    if (!(length2 > 0.0) || !std::isfinite(length2)) {
        if (valid) *valid = false;
        return defaultValue(input.type);
    }
    const double inverse = 1.0 / std::sqrt(length2);
    Value result = input;
    for (double& value : result.data) value *= inverse;
    if (valid) *valid = true;
    return result;
}

Value quaternionMultiply(const Value& a, const Value& b)
{
    const Value aa = broadcast(a, 4u), bb = broadcast(b, 4u);
    return vectorValue("float4", {
        aa.data[3] * bb.data[0] + aa.data[0] * bb.data[3] + aa.data[1] * bb.data[2] - aa.data[2] * bb.data[1],
        aa.data[3] * bb.data[1] - aa.data[0] * bb.data[2] + aa.data[1] * bb.data[3] + aa.data[2] * bb.data[0],
        aa.data[3] * bb.data[2] + aa.data[0] * bb.data[1] - aa.data[1] * bb.data[0] + aa.data[2] * bb.data[3],
        aa.data[3] * bb.data[3] - aa.data[0] * bb.data[0] - aa.data[1] * bb.data[1] - aa.data[2] * bb.data[2],
    });
}

Value quaternionSlerp(Value a, Value b, double t)
{
    a = normalized(broadcast(a, 4u));
    b = normalized(broadcast(b, 4u));
    double cosine = dot(a, b);
    if (cosine < 0.0) {
        cosine = -cosine;
        for (double& value : b.data) value = -value;
    }
    if (cosine > 0.9995) return normalized(componentwise(a, componentwise(b, a, [](double x, double y) { return x - y; }), [t](double x, double y) { return x + y * t; }));
    const double angle = std::acos(std::clamp(cosine, -1.0, 1.0));
    const double sine = std::sin(angle);
    if (std::abs(sine) < 1.0e-12) return a;
    const double wa = std::sin((1.0 - t) * angle) / sine;
    const double wb = std::sin(t * angle) / sine;
    Value result = a;
    for (std::size_t i = 0u; i < 4u; ++i) result.data[i] = a.data[i] * wa + b.data[i] * wb;
    return normalized(result);
}

std::array<double, 16> matrix4(const Value& source)
{
    std::array<double, 16> result{};
    result[0] = result[5] = result[10] = result[15] = 1.0;
    for (std::size_t i = 0u; i < std::min<std::size_t>(16u, source.data.size()); ++i) result[i] = source.data[i];
    return result;
}

Value matrixValue(const std::array<double, 16>& source)
{
    return {"float4x4", std::vector<double>(source.begin(), source.end())};
}

Value matrixMultiply(const Value& a, const Value& b)
{
    std::vector<double> result = InternalMath::multiply(a.type, a.data, b.type, b.data);
    if (result.empty()) return defaultValue(a.type);
    return {a.type, std::move(result)};
}

Value composeMatrix(Value translation, Value rotation, Value scale)
{
    translation = broadcast(translation, 3u);
    rotation = normalized(broadcast(rotation, 4u));
    scale = broadcast(scale, 3u);
    const double x = rotation.data[0], y = rotation.data[1], z = rotation.data[2], w = rotation.data[3];
    std::array<double, 16> m {{
        (1.0 - 2.0 * (y*y + z*z)) * scale.data[0], (2.0 * (x*y + w*z)) * scale.data[0], (2.0 * (x*z - w*y)) * scale.data[0], 0.0,
        (2.0 * (x*y - w*z)) * scale.data[1], (1.0 - 2.0 * (x*x + z*z)) * scale.data[1], (2.0 * (y*z + w*x)) * scale.data[1], 0.0,
        (2.0 * (x*z + w*y)) * scale.data[2], (2.0 * (y*z - w*x)) * scale.data[2], (1.0 - 2.0 * (x*x + y*y)) * scale.data[2], 0.0,
        translation.data[0], translation.data[1], translation.data[2], 1.0,
    }};
    return matrixValue(m);
}

bool invertMatrix(const Value& source, Value *output)
{
    if (!output) return false;
    std::vector<double> result;
    if (!InternalMath::inverse(source.type, source.data, &result)) return false;
    *output = {source.type, std::move(result)};
    return true;
}

std::string jsonPointerEscape(std::string value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

std::string valueString(const Value& value)
{
    if (value.type == "bool") return truthy(value) ? "true" : "false";
    if (value.type == "int" || value.type == "ref") return std::to_string(integer(value));
    std::ostringstream stream;
    if (!value.data.empty()) stream << value.data.front();
    return stream.str();
}

Models::Mat4 localMatrix(const Models::Runtime::NodeState& node)
{
    const double x = node.rotation.x, y = node.rotation.y, z = node.rotation.z, w = node.rotation.w;
    return {
        static_cast<float>((1.0 - 2.0 * (y*y + z*z)) * node.scale.x), static_cast<float>((2.0 * (x*y + w*z)) * node.scale.x), static_cast<float>((2.0 * (x*z - w*y)) * node.scale.x), 0.0f,
        static_cast<float>((2.0 * (x*y - w*z)) * node.scale.y), static_cast<float>((1.0 - 2.0 * (x*x + z*z)) * node.scale.y), static_cast<float>((2.0 * (y*z + w*x)) * node.scale.y), 0.0f,
        static_cast<float>((2.0 * (x*z + w*y)) * node.scale.z), static_cast<float>((2.0 * (y*z - w*x)) * node.scale.z), static_cast<float>((1.0 - 2.0 * (x*x + y*y)) * node.scale.z), 0.0f,
        node.translation.x, node.translation.y, node.translation.z, 1.0f,
    };
}

} // namespace

struct Runtime::Impl {
    Limits limits{};
    Statistics statistics{};
    Renderer::ModelScene::Instance *instance = nullptr;
    Models::Runtime::Pose pose{};
    Json::Value extension{};
    const Json::Value *graph = nullptr;
    std::string graph_name;
    std::vector<std::string> types;
    std::vector<Declaration> declarations;
    std::vector<const Json::Value *> nodes;
    std::vector<VariableState> variables;
    std::unordered_map<std::string, std::size_t> variable_names;
    std::vector<EventDefinition> events;
    std::unordered_map<std::string, std::size_t> event_ids;
    std::vector<std::unordered_map<std::string, Value>> transient_outputs;
    std::unordered_map<std::string, Value> pointer_values;
    std::vector<DelayState> delays;
    std::vector<InterpolationState> interpolations;
    std::vector<AnimationState> animations;
    std::deque<Event> emitted_events;
    std::uint64_t next_runtime_id = 1u;
    std::uint64_t activations_this_update = 0u;
    double time_since_start = 0.0;
    double previous_tick_time = 0.0;
    bool first_tick = true;
    bool loaded = false;
    bool pose_dirty = false;
    bool propagation_stopped = false;
    std::mt19937_64 random{0x48525345494E5445ull};

    const Json::Value *configuration(std::size_t node_index, std::string_view key) const
    {
        if (node_index >= nodes.size()) return nullptr;
        const Json::Value *configuration = nodes[node_index]->get("configuration");
        if (!configuration || !configuration->is(Json::Type::Object)) return nullptr;
        const auto found = configuration->object.find(std::string(key));
        if (found == configuration->object.end() || !found->second.is(Json::Type::Object)) return nullptr;
        return found->second.get("value");
    }

    int configInt(std::size_t node_index, std::string_view key, int fallback = -1) const
    {
        const Json::Value *value = configuration(node_index, key);
        if (!value) return fallback;
        if (value->is(Json::Type::Array) && !value->array.empty()) return Json::integer(&value->array.front(), fallback);
        return Json::integer(value, fallback);
    }

    double configFloat(std::size_t node_index, std::string_view key, double fallback = 0.0) const
    {
        const Json::Value *value = configuration(node_index, key);
        if (!value) return fallback;
        if (value->is(Json::Type::Array) && !value->array.empty()) return value->array.front().number;
        return value->is(Json::Type::Number) ? value->number : fallback;
    }

    bool configBool(std::size_t node_index, std::string_view key, bool fallback = false) const
    {
        const Json::Value *value = configuration(node_index, key);
        if (!value) return fallback;
        if (value->is(Json::Type::Array) && !value->array.empty()) return Json::boolValue(&value->array.front(), fallback);
        return Json::boolValue(value, fallback);
    }

    std::string configString(std::size_t node_index, std::string_view key, std::string fallback = {}) const
    {
        const Json::Value *value = configuration(node_index, key);
        if (!value) return fallback;
        if (value->is(Json::Type::String)) return value->string;
        if (value->is(Json::Type::Array) && !value->array.empty() && value->array.front().is(Json::Type::String))
            return value->array.front().string;
        return fallback;
    }

    std::string operation(std::size_t node_index) const
    {
        if (node_index >= nodes.size()) return {};
        const int declaration = Json::integer(nodes[node_index]->get("declaration"), -1);
        return declaration >= 0 && static_cast<std::size_t>(declaration) < declarations.size()
            ? declarations[static_cast<std::size_t>(declaration)].op : std::string{};
    }

    std::string declarationExtension(std::size_t node_index) const
    {
        if (node_index >= nodes.size()) return {};
        const int declaration = Json::integer(nodes[node_index]->get("declaration"), -1);
        return declaration >= 0 && static_cast<std::size_t>(declaration) < declarations.size()
            ? declarations[static_cast<std::size_t>(declaration)].extension : std::string{};
    }

    std::string typeName(int index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < types.size() ? types[static_cast<std::size_t>(index)] : "float";
    }

    const Json::Value *inputDescription(std::size_t node_index, std::string_view socket) const
    {
        if (node_index >= nodes.size()) return nullptr;
        const Json::Value *values = nodes[node_index]->get("values");
        if (!values || !values->is(Json::Type::Object)) return nullptr;
        const auto found = values->object.find(std::string(socket));
        return found == values->object.end() ? nullptr : &found->second;
    }

    Value evaluate(std::size_t node_index, std::string_view socket, std::string *error)
    {
        if (node_index >= nodes.size()) return {};
        if (const auto found = transient_outputs[node_index].find(std::string(socket)); found != transient_outputs[node_index].end())
            return found->second;

        const std::string op = operation(node_index);
        auto input = [&](std::string_view name, Value fallback = {}) -> Value {
            const Json::Value *description = inputDescription(node_index, name);
            if (!description || !description->is(Json::Type::Object)) return fallback;
            const int source_node = Json::integer(description->get("node"), -1);
            if (source_node >= 0) {
                if (static_cast<std::size_t>(source_node) >= node_index) {
                    if (error) *error = "KHR_interactivity value edge must reference an earlier node";
                    return fallback;
                }
                return evaluate(static_cast<std::size_t>(source_node), Json::stringValue(description->get("socket"), "value"), error);
            }
            const int type_index = Json::integer(description->get("type"), -1);
            return fromJson(typeName(type_index), description->get("value"));
        };

        if (op == "variable/get") {
            const int index = configInt(node_index, "variable", configInt(node_index, "index", -1));
            return index >= 0 && static_cast<std::size_t>(index) < variables.size() ? variables[static_cast<std::size_t>(index)].value : Value{};
        }
        if (op == "pointer/get") {
            const std::string pointer = resolvedPointer(node_index, input, error);
            if (socket == "isValid") return boolean(pointerGet(pointer).has_value());
            if (const auto value = pointerGet(pointer)) return *value;
            return input("default", Value{});
        }
        if (op == "event/onTick") {
            if (socket == "timeSinceStart") return scalar(time_since_start);
            if (socket == "timeSinceLastTick") return scalar(first_tick ? std::numeric_limits<double>::quiet_NaN() : time_since_start - previous_tick_time);
        }
        if (op == "event/receive") {
            const auto found = transient_outputs[node_index].find(std::string(socket));
            return found != transient_outputs[node_index].end() ? found->second : Value{};
        }
        if (op == "flow/doN" && socket == "currentCount") {
            const auto found = transient_outputs[node_index].find("__currentCount");
            return found != transient_outputs[node_index].end() ? found->second : integerValue(0);
        }
        if (op == "flow/for" && socket == "index") {
            const auto found = transient_outputs[node_index].find("__forIndex");
            return found != transient_outputs[node_index].end() ? found->second : integerValue(configInt(node_index, "initialIndex", 0));
        }
        if (op == "flow/waitAll" && socket == "remainingInputs") {
            const auto found = transient_outputs[node_index].find("__remainingInputs");
            return found != transient_outputs[node_index].end() ? found->second : integerValue(std::max(configInt(node_index, "inputFlows", 1), 1));
        }
        if (op == "flow/multiGate" && socket == "lastIndex") {
            const auto found = transient_outputs[node_index].find("__lastIndex");
            return found != transient_outputs[node_index].end() ? found->second : integerValue(-1);
        }
        if (op == "math/E") return scalar(std::numbers::e);
        if (op == "math/Pi") return scalar(std::numbers::pi);
        if (op == "math/Tau") return scalar(2.0 * std::numbers::pi);
        if (op == "math/Inf") return scalar(std::numeric_limits<double>::infinity());
        if (op == "math/NaN") return scalar(std::numeric_limits<double>::quiet_NaN());

        if (op.starts_with("type/")) {
            const Value a = input("a", input("value", {}));
            if (op == "type/boolToInt") return integerValue(truthy(a) ? 1 : 0);
            if (op == "type/boolToFloat") return scalar(truthy(a) ? 1.0 : 0.0);
            if (op == "type/intToBool" || op == "type/floatToBool") return boolean(truthy(a));
            if (op == "type/intToFloat") return scalar(static_cast<double>(integer(a)));
            if (op == "type/floatToInt") return integerValue(integer(a));
        }
        if (op == "ref/eq") return boolean(integer(input("a")) == integer(input("b")));

        if (op.starts_with("math/")) return evaluateMath(node_index, op, socket, input, error);
        return {};
    }

    Value evaluateMath(
        std::size_t node_index,
        const std::string& op,
        std::string_view socket,
        const auto& input,
        std::string *error)
    {
        (void)error;
        const Value a = input("a", {});
        const Value b = input("b", {});
        auto intBinary = [&](const auto& fn) {
            const std::uint32_t av = std::bit_cast<std::uint32_t>(integer(a));
            const std::uint32_t bv = std::bit_cast<std::uint32_t>(integer(b));
            return integerValue(std::bit_cast<std::int32_t>(fn(av, bv)));
        };
        if (op == "math/abs") {
            if (a.type == "int") {
                const std::int32_t v = integer(a);
                return integerValue(v == std::numeric_limits<std::int32_t>::min() ? v : std::abs(v));
            }
            return unary(a, [](double x) { return std::abs(x); });
        }
        if (op == "math/sign") return unary(a, [](double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : x); });
        if (op == "math/trunc") return unary(a, [](double x) { return std::trunc(x); });
        if (op == "math/floor") return unary(a, [](double x) { return std::floor(x); });
        if (op == "math/ceil") return unary(a, [](double x) { return std::ceil(x); });
        if (op == "math/round") return unary(a, [](double x) { return std::round(x); });
        if (op == "math/fract") return unary(a, [](double x) { return x - std::floor(x); });
        if (op == "math/neg") {
            if (a.type == "int") return intBinary([](std::uint32_t x, std::uint32_t) { return 0u - x; });
            return unary(a, [](double x) { return -x; });
        }
        if (op == "math/add") {
            if (a.type == "int") return intBinary([](std::uint32_t x, std::uint32_t y) { return x + y; });
            return componentwise(a, b, [](double x, double y) { return x + y; });
        }
        if (op == "math/sub") {
            if (a.type == "int") return intBinary([](std::uint32_t x, std::uint32_t y) { return x - y; });
            return componentwise(a, b, [](double x, double y) { return x - y; });
        }
        if (op == "math/mul") {
            if (a.type == "int") return intBinary([](std::uint32_t x, std::uint32_t y) { return x * y; });
            return componentwise(a, b, [](double x, double y) { return x * y; });
        }
        if (op == "math/div") {
            if (a.type == "int") {
                const std::int32_t av = integer(a), bv = integer(b);
                if (bv == 0) return integerValue(0);
                if (av == std::numeric_limits<std::int32_t>::min() && bv == -1) return integerValue(av);
                return integerValue(av / bv);
            }
            return componentwise(a, b, [](double x, double y) { return x / y; });
        }
        if (op == "math/rem") {
            if (a.type == "int") {
                const std::int32_t av = integer(a), bv = integer(b);
                return integerValue(bv == 0 ? 0 : (av == std::numeric_limits<std::int32_t>::min() && bv == -1 ? 0 : av % bv));
            }
            return componentwise(a, b, [](double x, double y) { return std::fmod(x, y); });
        }
        if (op == "math/min") return componentwise(a, b, [](double x, double y) { return std::min(x, y); });
        if (op == "math/max") return componentwise(a, b, [](double x, double y) { return std::max(x, y); });
        if (op == "math/clamp") {
            const Value c = input("c", {});
            const Value low = componentwise(b, c, [](double x, double y) { return std::min(x, y); });
            const Value high = componentwise(b, c, [](double x, double y) { return std::max(x, y); });
            return componentwise(componentwise(a, low, [](double x, double l) { return std::max(x, l); }), high, [](double x, double h) { return std::min(x, h); });
        }
        if (op == "math/saturate") return unary(a, [](double x) { return std::clamp(x, 0.0, 1.0); });
        if (op == "math/mix") {
            const Value t = input("t", input("c", scalar(0.0)));
            return componentwise(a, componentwise(componentwise(b, a, [](double x, double y) { return x - y; }), t, [](double x, double y) { return x * y; }), [](double x, double y) { return x + y; });
        }
        if (op == "math/smoothStep") {
            const Value x = input("c", {});
            Value t = componentwise(componentwise(x, a, [](double p, double q) { return p - q; }), componentwise(b, a, [](double p, double q) { return p - q; }), [](double p, double q) { return std::clamp(p / q, 0.0, 1.0); });
            return unary(t, [](double v) { return v * v * (3.0 - 2.0 * v); });
        }
        if (op == "math/eq") {
            if (a.data.size() != b.data.size()) return boolean(false);
            for (std::size_t i = 0u; i < a.data.size(); ++i) if (a.data[i] != b.data[i]) return boolean(false);
            return boolean(true);
        }
        if (op == "math/lt") return boolean(!a.data.empty() && !b.data.empty() && a.data[0] < b.data[0]);
        if (op == "math/le") return boolean(!a.data.empty() && !b.data.empty() && a.data[0] <= b.data[0]);
        if (op == "math/gt") return boolean(!a.data.empty() && !b.data.empty() && a.data[0] > b.data[0]);
        if (op == "math/ge") return boolean(!a.data.empty() && !b.data.empty() && a.data[0] >= b.data[0]);
        if (op == "math/isNaN") return boolean(!a.data.empty() && std::isnan(a.data[0]));
        if (op == "math/isInf") return boolean(!a.data.empty() && std::isinf(a.data[0]));
        if (op == "math/select") return truthy(input("condition", input("select", {}))) ? a : b;
        if (op == "math/switch") {
            const int selection = integer(input("selection", input("index", {})));
            const std::string key = std::to_string(selection);
            const Json::Value *values = nodes[node_index]->get("values");
            if (values && values->is(Json::Type::Object) && values->object.contains(key)) return input(key, input("default", {}));
            return input("default", {});
        }
        if (op == "math/random") {
            std::uniform_real_distribution<double> distribution(0.0, 1.0);
            return scalar(distribution(random));
        }
        if (op == "math/rad") return unary(a, [](double x) { return x * std::numbers::pi / 180.0; });
        if (op == "math/deg") return unary(a, [](double x) { return x * 180.0 / std::numbers::pi; });
        if (op == "math/sin") return unary(a, [](double x) { return std::sin(x); });
        if (op == "math/cos") return unary(a, [](double x) { return std::cos(x); });
        if (op == "math/tan") return unary(a, [](double x) { return std::tan(x); });
        if (op == "math/asin") return unary(a, [](double x) { return std::asin(x); });
        if (op == "math/acos") return unary(a, [](double x) { return std::acos(x); });
        if (op == "math/atan") return unary(a, [](double x) { return std::atan(x); });
        if (op == "math/atan2") return componentwise(a, b, [](double y, double x) { return std::atan2(y, x); });
        if (op == "math/sinh") return unary(a, [](double x) { return std::sinh(x); });
        if (op == "math/cosh") return unary(a, [](double x) { return std::cosh(x); });
        if (op == "math/tanh") return unary(a, [](double x) { return std::tanh(x); });
        if (op == "math/asinh") return unary(a, [](double x) { return std::asinh(x); });
        if (op == "math/acosh") return unary(a, [](double x) { return std::acosh(x); });
        if (op == "math/atanh") return unary(a, [](double x) { return std::atanh(x); });
        if (op == "math/exp") return unary(a, [](double x) { return std::exp(x); });
        if (op == "math/log") return unary(a, [](double x) { return std::log(x); });
        if (op == "math/log2") return unary(a, [](double x) { return std::log2(x); });
        if (op == "math/log10") return unary(a, [](double x) { return std::log10(x); });
        if (op == "math/sqrt") return unary(a, [](double x) { return std::sqrt(x); });
        if (op == "math/cbrt") return unary(a, [](double x) { return std::cbrt(x); });
        if (op == "math/pow") return componentwise(a, b, [](double x, double y) { return std::pow(x, y); });
        if (op == "math/length") return scalar(std::sqrt(dot(a, a)));
        if (op == "math/normalize") {
            bool valid = false;
            Value value = normalized(a, &valid);
            if (socket == "isValid") return boolean(valid);
            return value;
        }
        if (op == "math/dot") return scalar(dot(a, b));
        if (op == "math/cross") {
            const Value aa = broadcast(a, 3u), bb = broadcast(b, 3u);
            return vectorValue("float3", {aa.data[1]*bb.data[2]-aa.data[2]*bb.data[1], aa.data[2]*bb.data[0]-aa.data[0]*bb.data[2], aa.data[0]*bb.data[1]-aa.data[1]*bb.data[0]});
        }
        if (op == "math/rotate2D") {
            const Value aa = broadcast(a, 2u); const double angle = input("angle", b).data[0];
            return vectorValue("float2", {aa.data[0]*std::cos(angle)-aa.data[1]*std::sin(angle), aa.data[0]*std::sin(angle)+aa.data[1]*std::cos(angle)});
        }
        if (op == "math/rotate3D") {
            Value q = normalized(broadcast(input("rotation", b), 4u));
            Value v = broadcast(a, 3u);
            Value p = vectorValue("float4", {v.data[0], v.data[1], v.data[2], 0.0});
            Value qc = vectorValue("float4", {-q.data[0], -q.data[1], -q.data[2], q.data[3]});
            Value r = quaternionMultiply(quaternionMultiply(q, p), qc);
            return vectorValue("float3", {r.data[0], r.data[1], r.data[2]});
        }
        if (op == "math/transform") {
            const Value matrix = input("matrix", b);
            const std::size_t n = InternalMath::matrixDimension(matrix.type);
            if (n >= 2u && n <= 4u && a.data.size() >= n && matrix.data.size() >= n * n) {
                Value result = a;
                result.data.assign(n, 0.0);
                for (std::size_t row = 0u; row < n; ++row)
                    for (std::size_t column = 0u; column < n; ++column)
                        result.data[row] += matrix.data[column * n + row] * a.data[column];
                return result;
            }
            return defaultValue(a.type);
        }
        if (op == "math/slerp" || op == "math/quatSlerp") return quaternionSlerp(a, b, input("t", scalar(0.0)).data[0]);
        if (op == "math/transpose") {
            std::vector<double> result = InternalMath::transpose(a.type, a.data);
            return result.empty() ? defaultValue(a.type) : Value{a.type, std::move(result)};
        }
        if (op == "math/determinant") return scalar(InternalMath::determinant(a.type, a.data));
        if (op == "math/inverse") { Value inverse; const bool valid=invertMatrix(a,&inverse); if(socket=="isValid")return boolean(valid); return valid?inverse:defaultValue(a.type); }
        if (op == "math/matMul") return matrixMultiply(a, b);
        if (op == "math/matCompose") return composeMatrix(input("translation", {}), input("rotation", vectorValue("float4",{0,0,0,1})), input("scale", vectorValue("float3",{1,1,1})));
        if (op == "math/matDecompose") {
            const InternalMath::DecomposedTransform decomposed = InternalMath::decomposeTransform(a.data);
            if (socket == "translation") return vectorValue("float3", {decomposed.translation[0], decomposed.translation[1], decomposed.translation[2]});
            if (socket == "rotation") return vectorValue("float4", {decomposed.rotation[0], decomposed.rotation[1], decomposed.rotation[2], decomposed.rotation[3]});
            if (socket == "scale") return vectorValue("float3", {decomposed.scale[0], decomposed.scale[1], decomposed.scale[2]});
            return {};
        }
        if (op == "math/quatConjugate") { Value q=broadcast(a,4u); q.data[0]=-q.data[0];q.data[1]=-q.data[1];q.data[2]=-q.data[2];return q; }
        if (op == "math/quatMul") return quaternionMultiply(a,b);
        if (op == "math/quatAngleBetween") { const Value aa=normalized(a),bb=normalized(b);return scalar(2.0*std::acos(std::clamp(std::abs(dot(aa,bb)),0.0,1.0))); }
        if (op == "math/quatFromAxisAngle") { Value axis=normalized(input("axis",a));const double angle=input("angle",b).data[0],s=std::sin(angle*0.5);return vectorValue("float4",{axis.data[0]*s,axis.data[1]*s,axis.data[2]*s,std::cos(angle*0.5)}); }
        if (op == "math/quatFromAngles") {
            const InternalMath::Quat q = InternalMath::quatFromAngles(input("x", scalar(0.0)).data[0], input("y", scalar(0.0)).data[0], input("z", scalar(0.0)).data[0], configString(node_index, "order", "yxz"));
            return vectorValue("float4", {q[0], q[1], q[2], q[3]});
        }
        if (op == "math/quatFromDirections") {
            const Value aa = broadcast(a, 3u), bb = broadcast(b, 3u);
            const InternalMath::Quat q = InternalMath::quatFromDirections({aa.data[0], aa.data[1], aa.data[2]}, {bb.data[0], bb.data[1], bb.data[2]});
            return vectorValue("float4", {q[0], q[1], q[2], q[3]});
        }
        if (op == "math/quatFromUpForward") {
            const Value up = broadcast(input("up", {}), 3u), forward = broadcast(input("forward", {}), 3u);
            const InternalMath::Quat q = InternalMath::quatFromUpForward({up.data[0], up.data[1], up.data[2]}, {forward.data[0], forward.data[1], forward.data[2]});
            return vectorValue("float4", {q[0], q[1], q[2], q[3]});
        }
        if (op == "math/quatToAxisAngle") { Value q=normalized(a);const double angle=2.0*std::acos(std::clamp(q.data[3],-1.0,1.0)),s=std::sqrt(std::max(0.0,1.0-q.data[3]*q.data[3]));if(socket=="angle")return scalar(angle);return s<1e-8?vectorValue("float3",{1,0,0}):vectorValue("float3",{q.data[0]/s,q.data[1]/s,q.data[2]/s}); }
        if (op == "math/rgbToOkLCh") {
            const InternalMath::Vec3 result = InternalMath::rgbToOkLCh(input("r", scalar(0.0)).data[0], input("g", scalar(0.0)).data[0], input("b", scalar(0.0)).data[0]);
            if (socket == "l") return scalar(result[0]);
            if (socket == "c") return scalar(result[1]);
            if (socket == "h") return scalar(result[2]);
            return {};
        }
        if (op == "math/rgbFromOkLCh") {
            const InternalMath::Vec3 result = InternalMath::rgbFromOkLCh(input("l", scalar(0.0)).data[0], input("c", scalar(0.0)).data[0], input("h", scalar(0.0)).data[0]);
            if (socket == "r") return scalar(result[0]);
            if (socket == "g") return scalar(result[1]);
            if (socket == "b") return scalar(result[2]);
            return {};
        }
        if (op == "math/not") return integerValue(~integer(a));
        if (op == "math/and") return intBinary([](std::uint32_t x,std::uint32_t y){return x&y;});
        if (op == "math/or") return intBinary([](std::uint32_t x,std::uint32_t y){return x|y;});
        if (op == "math/xor") return intBinary([](std::uint32_t x,std::uint32_t y){return x^y;});
        if (op == "math/asr") return integerValue(integer(a) >> (static_cast<std::uint32_t>(integer(b)) & 31u));
        if (op == "math/lsl") return intBinary([](std::uint32_t x,std::uint32_t y){return x<<(y&31u);});
        if (op == "math/clz") return integerValue(integer(a)==0?32:static_cast<std::int32_t>(std::countl_zero(std::bit_cast<std::uint32_t>(integer(a)))));
        if (op == "math/ctz") return integerValue(integer(a)==0?32:static_cast<std::int32_t>(std::countr_zero(std::bit_cast<std::uint32_t>(integer(a)))));
        if (op == "math/popcnt") return integerValue(static_cast<std::int32_t>(std::popcount(std::bit_cast<std::uint32_t>(integer(a)))));

        if (op.starts_with("math/combine")) {
            const std::size_t count = op == "math/combine2" ? 2u : op == "math/combine3" ? 3u : op == "math/combine4" ? 4u : op == "math/combine2x2" ? 4u : op == "math/combine3x3" ? 9u : op == "math/combine4x4" ? 16u : 0u;
            if (count == 0u) return {};
            std::vector<double> components;
            for(char name='a'; components.size()<count && name<='p'; ++name){std::string key(1,name);Value c=input(key,{});if(c.data.empty())break;components.push_back(c.data[0]);}
            if (components.size() != count) return {};
            const std::string type = op == "math/combine2" ? "float2" : op == "math/combine3" ? "float3" : op == "math/combine4" ? "float4" : op == "math/combine2x2" ? "float2x2" : op == "math/combine3x3" ? "float3x3" : "float4x4";
            return {type,std::move(components)};
        }
        if (op.starts_with("math/extract")) {
            int index = configInt(node_index,"index",-1);
            if (index < 0 && !socket.empty()) {
                std::size_t parsed = 0u;
                bool numeric = true;
                for (const char digit : socket) {
                    if (digit < '0' || digit > '9') { numeric = false; break; }
                    parsed = parsed * 10u + static_cast<std::size_t>(digit - '0');
                    if (parsed > 15u) { numeric = false; break; }
                }
                if (numeric) index = static_cast<int>(parsed);
            }
            if (index >= 0 && static_cast<std::size_t>(index) < a.data.size()) return scalar(a.data[static_cast<std::size_t>(index)]);
        }
        return {};
    }

    template<class InputFn>
    std::string resolvedPointer(std::size_t node_index, const InputFn& input, std::string *error)
    {
        std::string pointer = configString(node_index, "pointer");
        if (pointer.empty()) return pointer;
        std::unordered_set<std::string> parameters;
        for (std::size_t i = 0u; i < pointer.size();) {
            if (i + 1u < pointer.size() && ((pointer[i]=='{'&&pointer[i+1u]=='{') || (pointer[i]=='['&&pointer[i+1u]=='['))) { pointer.erase(i,1u); i+=1u; continue; }
            if (pointer[i] != '{' && pointer[i] != '[') { ++i; continue; }
            const char close = pointer[i]=='{' ? '}' : ']';
            const std::size_t end = pointer.find(close, i+1u);
            if (end == std::string::npos) { if(error)*error="invalid KHR_interactivity pointer template"; return {}; }
            const std::string parameter = pointer.substr(i+1u,end-i-1u);
            if (parameter.empty() || !parameters.insert(parameter).second) { if(error)*error="invalid repeated/empty pointer template parameter"; return {}; }
            const Value replacement = input(parameter, {});
            const std::string text = jsonPointerEscape(valueString(replacement));
            pointer.replace(i,end-i+1u,text);
            i += text.size();
        }
        return pointer;
    }

    std::optional<Value> pointerGet(const std::string& pointer) const
    {
        if (const auto found=pointer_values.find(pointer);found!=pointer_values.end()) return found->second;
        if (pointer.starts_with("/nodes/")) {
            const std::size_t slash=pointer.find('/',7u); if(slash==std::string::npos)return std::nullopt;
            const std::size_t index=static_cast<std::size_t>(std::strtoull(pointer.substr(7u,slash-7u).c_str(),nullptr,10));
            if(index>=pose.nodes.size())return std::nullopt; const auto& n=pose.nodes[index]; const std::string property=pointer.substr(slash+1u);
            if(property=="translation")return vectorValue("float3",{n.translation.x,n.translation.y,n.translation.z});
            if(property=="rotation")return vectorValue("float4",{n.rotation.x,n.rotation.y,n.rotation.z,n.rotation.w});
            if(property=="scale")return vectorValue("float3",{n.scale.x,n.scale.y,n.scale.z});
            if(property=="weights"){Value v{"float",{}};for(float x:n.weights)v.data.push_back(x);return v;}
            if(property=="extensions/KHR_node_visibility/visible")return boolean(n.visible);
            if(property=="extensions/KHR_node_selectability/selectable")return boolean(n.selectable);
            if(property=="extensions/KHR_node_hoverability/hoverable")return boolean(n.hoverable);
        }
        return std::nullopt;
    }

    bool pointerSet(Ecs::World& world, const std::string& pointer, const Value& value, std::string *error)
    {
        if(pointer.empty())return false;
        pointer_values.insert_or_assign(pointer,value);
        if(pointer.starts_with("/nodes/")) {
            const std::size_t slash=pointer.find('/',7u); if(slash==std::string::npos)return false;
            const std::size_t index=static_cast<std::size_t>(std::strtoull(pointer.substr(7u,slash-7u).c_str(),nullptr,10));
            if(index>=pose.nodes.size())return false; auto& n=pose.nodes[index];const std::string property=pointer.substr(slash+1u);
            if(property=="translation"&&value.data.size()>=3u)n.translation={static_cast<float>(value.data[0]),static_cast<float>(value.data[1]),static_cast<float>(value.data[2])};
            else if(property=="rotation"&&value.data.size()>=4u)n.rotation={static_cast<float>(value.data[0]),static_cast<float>(value.data[1]),static_cast<float>(value.data[2]),static_cast<float>(value.data[3])};
            else if(property=="scale"&&value.data.size()>=3u)n.scale={static_cast<float>(value.data[0]),static_cast<float>(value.data[1]),static_cast<float>(value.data[2])};
            else if(property=="weights"){n.weights.clear();for(double x:value.data)n.weights.push_back(static_cast<float>(x));}
            else if(property=="extensions/KHR_node_visibility/visible")n.visible=truthy(value);
            else if(property=="extensions/KHR_node_selectability/selectable")n.selectable=truthy(value);
            else if(property=="extensions/KHR_node_hoverability/hoverable")n.hoverable=truthy(value);
            n.local=localMatrix(n); pose_dirty=true;
        } else {
            std::vector<float> values;values.reserve(value.data.size());for(double x:value.data)values.push_back(static_cast<float>(x));pose.pointer_values.insert_or_assign(pointer,std::move(values));pose_dirty=true;
        }
        ++statistics.pointer_writes;
        if(pose_dirty && instance && !Renderer::ModelScene::applyPose(world,*instance,pose,error))return false;
        pose_dirty=false; return true;
    }

    std::optional<FlowTarget> flow(std::size_t node_index, std::string_view output) const
    {
        if(node_index>=nodes.size())return std::nullopt;const Json::Value* flows=nodes[node_index]->get("flows");if(!flows||!flows->is(Json::Type::Object))return std::nullopt;
        const auto found=flows->object.find(std::string(output));if(found==flows->object.end()||!found->second.is(Json::Type::Object))return std::nullopt;
        const int target=Json::integer(found->second.get("node"),-1);if(target<0||static_cast<std::size_t>(target)>=nodes.size())return std::nullopt;
        return FlowTarget{static_cast<std::size_t>(target),Json::stringValue(found->second.get("socket"),"in")};
    }

    bool emit(Ecs::World& world,std::size_t node_index,std::string_view output,std::string*error)
    {
        if(const auto target=flow(node_index,output))return activate(world,target->node,target->socket,error);return true;
    }

    bool activate(Ecs::World& world,std::size_t node_index,std::string_view input_socket,std::string*error)
    {
        if(node_index>=nodes.size())return fail(error,"KHR_interactivity flow targets invalid node");
        if(++activations_this_update>limits.max_activations_per_update)return fail(error,"KHR_interactivity activation budget exceeded");
        ++statistics.activations;
        const std::string op=operation(node_index);
        const std::string extension=declarationExtension(node_index);
        if(!extension.empty())return emit(world,node_index,"out",error);
        auto value=[&](std::string_view name,Value fallback={}){const Json::Value* d=inputDescription(node_index,name);if(!d||!d->is(Json::Type::Object))return fallback;const int n=Json::integer(d->get("node"),-1);if(n>=0)return evaluate(static_cast<std::size_t>(n),Json::stringValue(d->get("socket"),"value"),error);return fromJson(typeName(Json::integer(d->get("type"),-1)),d->get("value"));};

        if(op=="event/onStart"||op=="event/onTick"||op=="event/receive")return emit(world,node_index,"out",error);
        if(op=="event/stopPropagation"){propagation_stopped=true;return emit(world,node_index,"out",error);}
        if(op=="event/send"){
            const int event_index=configInt(node_index,"event",configInt(node_index,"index",-1));
            if(event_index<0||static_cast<std::size_t>(event_index)>=events.size())return emit(world,node_index,"out",error);
            Event event;event.id=events[event_index].id;for(const auto&[socket_name,type_index]:events[event_index].values)event.values.push_back({socket_name,value(socket_name,defaultValue(typeName(static_cast<int>(type_index)))).data});
            emitted_events.push_back(event);++statistics.events;dispatchEvent(world,event,error);return emit(world,node_index,"out",error);
        }
        if(op=="variable/set"){
            const int index=configInt(node_index,"variable",configInt(node_index,"index",-1));if(index>=0&&static_cast<std::size_t>(index)<variables.size())variables[static_cast<std::size_t>(index)].value=value("value",variables[static_cast<std::size_t>(index)].value);return emit(world,node_index,"out",error);
        }
        if(op=="variable/interpolate"){
            const int index=configInt(node_index,"variable",configInt(node_index,"index",-1));if(index<0||static_cast<std::size_t>(index)>=variables.size())return emit(world,node_index,"err",error);
            if(interpolations.size()>=limits.max_active_interpolations)return fail(error,"KHR_interactivity interpolation limit exceeded");
            InterpolationState state;state.kind=InterpolationState::Kind::Variable;state.variable=static_cast<std::size_t>(index);state.from=variables[state.variable].value;state.to=value("value",state.from);state.duration=std::max(0.0,value("duration",scalar(0)).data[0]);state.easing=configString(node_index,"easing","linear");if(auto done=flow(node_index,"done"))state.done=*done;interpolations.push_back(std::move(state));return emit(world,node_index,"out",error);
        }
        if(op=="pointer/set"){
            const std::string pointer=resolvedPointer(node_index,value,error);if(!pointerSet(world,pointer,value("value",{}),error))return emit(world,node_index,"err",error);return emit(world,node_index,"out",error);
        }
        if(op=="pointer/interpolate"){
            if(interpolations.size()>=limits.max_active_interpolations)return fail(error,"KHR_interactivity interpolation limit exceeded");
            const std::string pointer=resolvedPointer(node_index,value,error);InterpolationState state;state.kind=InterpolationState::Kind::Pointer;state.pointer=pointer;state.from=pointerGet(pointer).value_or(value("value",{}));state.to=value("value",state.from);state.duration=std::max(0.0,value("duration",scalar(0)).data[0]);state.easing=configString(node_index,"easing","linear");if(auto done=flow(node_index,"done"))state.done=*done;interpolations.push_back(std::move(state));return emit(world,node_index,"out",error);
        }
        if(op=="flow/branch")return emit(world,node_index,truthy(value("condition",value("a",{})))?"true":"false",error);
        if(op=="flow/switch"){
            const int selection=integer(value("selection",value("index",{})));if(flow(node_index,std::to_string(selection)))return emit(world,node_index,std::to_string(selection),error);return emit(world,node_index,"default",error);
        }
        if(op=="flow/sequence"){
            const Json::Value* flows=nodes[node_index]->get("flows");if(!flows||!flows->is(Json::Type::Object))return true;std::vector<std::string> order;order.reserve(flows->object.size());for(const auto&[name,_]:flows->object)order.push_back(name);std::sort(order.begin(),order.end(),socketIdLess);for(const std::string&name:order)if(!emit(world,node_index,name,error))return false;return true;
        }
        if(op=="flow/doN"){
            Value& stored=transient_outputs[node_index]["__currentCount"];
            if(stored.type!="int")stored=integerValue(0);
            if(input_socket=="reset"){stored=integerValue(0);return true;}
            const int limit=std::max(integer(value("n",integerValue(0))),0);
            int count=integer(stored);
            if(count>=limit)return true;
            stored=integerValue(++count);
            return emit(world,node_index,"out",error);
        }
        if(op=="flow/for"){
            const int start=integer(value("startIndex",integerValue(0)));
            const int end=integer(value("endIndex",integerValue(0)));
            transient_outputs[node_index]["__forIndex"]=integerValue(start);
            for(int index=start;index<end;++index){
                transient_outputs[node_index]["__forIndex"]=integerValue(index);
                if(!emit(world,node_index,"loopBody",error)&&!emit(world,node_index,"body",error))return false;
            }
            transient_outputs[node_index]["__forIndex"]=integerValue(end);
            return emit(world,node_index,"completed",error);
        }
        if(op=="flow/while"){
            std::size_t guard=0u;while(truthy(value("condition",{}))){if(++guard>limits.max_activations_per_update)return fail(error,"KHR_interactivity while loop budget exceeded");if(!emit(world,node_index,"loopBody",error)&&!emit(world,node_index,"body",error))return false;}return emit(world,node_index,"completed",error);
        }
        if(op=="flow/setDelay"){
            if(delays.size()>=limits.max_active_delays)return fail(error,"KHR_interactivity delay limit exceeded");DelayState delay;delay.id=next_runtime_id++;delay.remaining=std::max(0.0,value("duration",value("delay",scalar(0))).data[0]);if(auto target=flow(node_index,"out"))delay.target=*target;transient_outputs[node_index]["delayId"]={"ref",{static_cast<double>(delay.id)}};delays.push_back(delay);return true;
        }
        if(op=="flow/cancelDelay"){
            const std::uint64_t id=static_cast<std::uint64_t>(std::max(0.0,value("delayId",value("id",{})).data[0]));delays.erase(std::remove_if(delays.begin(),delays.end(),[&](const DelayState&d){return d.id==id;}),delays.end());return emit(world,node_index,"out",error);
        }
        if(op=="flow/throttle"){
            const double duration=std::max(0.0,value("duration",scalar(0)).data[0]);const std::string key="__last";double last=-std::numeric_limits<double>::infinity();if(const auto it=transient_outputs[node_index].find(key);it!=transient_outputs[node_index].end()&&!it->second.data.empty())last=it->second.data[0];if(time_since_start-last>=duration){transient_outputs[node_index][key]=scalar(time_since_start);return emit(world,node_index,"out",error);}return emit(world,node_index,"err",error);
        }
        if(op=="flow/multiGate"){
            const Json::Value* flows=nodes[node_index]->get("flows");
            std::vector<std::string> outputs;
            if(flows&&flows->is(Json::Type::Object)){outputs.reserve(flows->object.size());for(const auto&[name,_]:flows->object)outputs.push_back(name);}
            std::sort(outputs.begin(),outputs.end(),socketIdLess);
            Value& last=transient_outputs[node_index]["__lastIndex"];
            if(last.type!="int")last=integerValue(-1);
            auto reset=[&](){last=integerValue(-1);for(std::size_t i=0u;i<outputs.size();++i)transient_outputs[node_index]["__multiUsed"+std::to_string(i)]=boolean(false);};
            if(input_socket=="reset"){reset();return true;}
            if(outputs.empty())return true;
            const bool is_loop=configBool(node_index,"isLoop",false);
            const bool is_random=configBool(node_index,"isRandom",false);
            std::vector<std::size_t> available;
            available.reserve(outputs.size());
            for(std::size_t i=0u;i<outputs.size();++i){const auto found=transient_outputs[node_index].find("__multiUsed"+std::to_string(i));if(found==transient_outputs[node_index].end()||!truthy(found->second))available.push_back(i);}
            if(available.empty()){
                if(!is_loop)return true;
                reset();
                available.resize(outputs.size());for(std::size_t i=0u;i<outputs.size();++i)available[i]=i;
            }
            std::size_t selected=available.front();
            if(is_random){std::uniform_int_distribution<std::size_t> distribution(0u,available.size()-1u);selected=available[distribution(random)];}
            transient_outputs[node_index]["__multiUsed"+std::to_string(selected)]=boolean(true);
            last=integerValue(static_cast<std::int32_t>(selected));
            return emit(world,node_index,outputs[selected],error);
        }
        if(op=="flow/waitAll"){
            const int input_count=std::max(configInt(node_index,"inputFlows",1),1);
            Value& remaining=transient_outputs[node_index]["__remainingInputs"];
            if(remaining.type!="int")remaining=integerValue(input_count);
            auto reset=[&](){remaining=integerValue(input_count);for(int i=0;i<input_count;++i)transient_outputs[node_index]["__wait_"+std::to_string(i)]=boolean(false);};
            if(input_socket=="reset"){reset();return true;}
            std::size_t parsed=0u;bool numeric=!input_socket.empty();for(const char digit:input_socket){if(digit<'0'||digit>'9'){numeric=false;break;}parsed=parsed*10u+static_cast<std::size_t>(digit-'0');if(parsed>=static_cast<std::size_t>(input_count)){numeric=false;break;}}
            if(!numeric)return true;
            const std::string key="__wait_"+std::to_string(parsed);
            if(!truthy(transient_outputs[node_index][key])){transient_outputs[node_index][key]=boolean(true);remaining=integerValue(std::max(integer(remaining)-1,0));}
            if(integer(remaining)==0)return emit(world,node_index,"out",error);
            return true;
        }
        if(op=="animation/start"){
            if(animations.size()>=limits.max_active_animations)return fail(error,"KHR_interactivity animation limit exceeded");const int index=integer(value("animation",integerValue(configInt(node_index,"animation",-1))));if(index<0||static_cast<std::size_t>(index)>=Models::modelAnimationCount(instance->model))return emit(world,node_index,"err",error);AnimationState state;state.id=next_runtime_id++;state.animation=static_cast<std::size_t>(index);state.speed=value("speed",scalar(1)).data[0];state.start=value("startTime",scalar(0)).data[0];state.time=state.start;state.loop=truthy(value("loop",boolean(false)));if(const Models::ModelAnimationData*animation=Models::modelAnimation(instance->model,state.animation))state.end=animation->duration;animations.push_back(state);transient_outputs[node_index]["animationId"]={"ref",{static_cast<double>(state.id)}};return emit(world,node_index,"out",error);
        }
        if(op=="animation/stop"||op=="animation/stopAt"){
            const std::uint64_t id=static_cast<std::uint64_t>(std::max(0.0,value("animationId",value("id",{})).data[0]));if(op=="animation/stop")animations.erase(std::remove_if(animations.begin(),animations.end(),[&](const AnimationState&a){return a.id==id;}),animations.end());else for(AnimationState&a:animations)if(a.id==id){a.time=value("time",scalar(a.time)).data[0];a.paused=true;}return emit(world,node_index,"out",error);
        }
        if(op=="debug/log"){
            Value message=value("message",value("value",{}));std::fprintf(stderr,"[KHR_interactivity] %s\n",valueString(message).c_str());return emit(world,node_index,"out",error);
        }
        return emit(world,node_index,"out",error);
    }

    bool dispatchEvent(Ecs::World& world,const Event&event,std::string*error)
    {
        propagation_stopped=false;
        for(std::size_t i=0u;i<nodes.size()&&!propagation_stopped;++i){if(operation(i)!="event/receive")continue;const int event_index=configInt(i,"event",configInt(i,"index",-1));if(event_index<0||static_cast<std::size_t>(event_index)>=events.size()||events[event_index].id!=event.id)continue;for(const EventValue&v:event.values){Value output{"float",v.value};const auto type=events[event_index].values.find(v.socket);if(type!=events[event_index].values.end())output.type=typeName(static_cast<int>(type->second));transient_outputs[i][v.socket]=std::move(output);}if(!activate(world,i,"in",error))return false;}return true;
    }

    double easing(std::string_view name,double t) const
    {
        t=std::clamp(t,0.0,1.0);if(name=="step")return t>=1.0?1.0:0.0;if(name=="easeIn")return t*t;if(name=="easeOut")return 1.0-(1.0-t)*(1.0-t);if(name=="easeInOut")return t*t*(3.0-2.0*t);return t;
    }

    bool updateTimed(Ecs::World&world,double delta,std::string*error)
    {
        for(std::size_t i=0u;i<delays.size();){delays[i].remaining-=delta;if(delays[i].remaining<=0.0){const FlowTarget target=delays[i].target;delays.erase(delays.begin()+static_cast<std::ptrdiff_t>(i));if(target.node!=SIZE_MAX&&!activate(world,target.node,target.socket,error))return false;}else++i;}
        for(std::size_t i=0u;i<interpolations.size();){InterpolationState&state=interpolations[i];state.elapsed+=delta;const double t=state.duration<=0.0?1.0:easing(state.easing,state.elapsed/state.duration);Value current=componentwise(state.from,componentwise(state.to,state.from,[](double x,double y){return x-y;}),[t](double x,double y){return x+y*t;});if(state.kind==InterpolationState::Kind::Variable)variables[state.variable].value=current;else if(!pointerSet(world,state.pointer,current,error))return false;if(t>=1.0){const FlowTarget done=state.done;interpolations.erase(interpolations.begin()+static_cast<std::ptrdiff_t>(i));if(done.node!=SIZE_MAX&&!activate(world,done.node,done.socket,error))return false;}else++i;}
        if(!animations.empty()){
            for(std::size_t i=0u;i<animations.size();){AnimationState&state=animations[i];if(!state.paused)state.time+=delta*state.speed;const Models::ModelAnimationData*clip=Models::modelAnimation(instance->model,state.animation);const double duration=clip?clip->duration:0.0;if(state.loop&&duration>0.0)state.time=std::fmod(std::max(state.time,0.0),duration);else if(state.time>state.end){animations.erase(animations.begin()+static_cast<std::ptrdiff_t>(i));continue;}Models::Runtime::Pose sampled;if(!Models::Runtime::sample(instance->model,state.animation,static_cast<float>(state.time),state.loop,&sampled,error))return false;for(std::size_t n=0u;n<std::min(pose.nodes.size(),sampled.nodes.size());++n){pose.nodes[n]=sampled.nodes[n];}for(const auto&[pointer,values]:sampled.pointer_values)pose.pointer_values[pointer]=values;pose_dirty=true;++statistics.animation_updates;++i;}if(pose_dirty&&!Renderer::ModelScene::applyPose(world,*instance,pose,error))return false;pose_dirty=false;}
        return true;
    }

    bool parseGraph(std::string*error)
    {
        const Json::Value*graphs=extension.get("graphs");if(!graphs||!graphs->is(Json::Type::Array)||graphs->array.empty())return fail(error,"KHR_interactivity graphs is missing or empty");
        const int selected=Json::integer(extension.get("graph"),0);if(selected<0||static_cast<std::size_t>(selected)>=graphs->array.size())return fail(error,"KHR_interactivity active graph index is invalid");graph=&graphs->array[static_cast<std::size_t>(selected)];if(!graph->is(Json::Type::Object))return fail(error,"KHR_interactivity graph is not an object");graph_name=Json::stringValue(graph->get("name"));
        const Json::Value*type_array=graph->get("types");const Json::Value*declaration_array=graph->get("declarations");const Json::Value*node_array=graph->get("nodes");if(!type_array||!type_array->is(Json::Type::Array)||!declaration_array||!declaration_array->is(Json::Type::Array)||!node_array||!node_array->is(Json::Type::Array))return fail(error,"KHR_interactivity graph requires types, declarations, and nodes arrays");if(node_array->array.size()>limits.max_nodes)return fail(error,"KHR_interactivity graph exceeds node limit");
        types.clear();for(const Json::Value&t:type_array->array){if(!t.is(Json::Type::Object))return fail(error,"KHR_interactivity type is invalid");types.push_back(Json::stringValue(t.get("signature")));if(types.back().empty())return fail(error,"KHR_interactivity type signature is empty");}
        declarations.clear();for(const Json::Value&d:declaration_array->array){if(!d.is(Json::Type::Object))return fail(error,"KHR_interactivity declaration is invalid");Declaration declaration;declaration.op=Json::stringValue(d.get("op"));declaration.extension=Json::stringValue(d.get("extension"));if(declaration.op.empty())return fail(error,"KHR_interactivity declaration op is empty");declarations.push_back(std::move(declaration));}
        nodes.clear();nodes.reserve(node_array->array.size());for(std::size_t i=0u;i<node_array->array.size();++i){const Json::Value&n= node_array->array[i];if(!n.is(Json::Type::Object))return fail(error,"KHR_interactivity node is invalid");const int declaration=Json::integer(n.get("declaration"),-1);if(declaration<0||static_cast<std::size_t>(declaration)>=declarations.size())return fail(error,"KHR_interactivity node declaration is invalid");if(const Json::Value*values=n.get("values");values&&values->is(Json::Type::Object))for(const auto&[_,v]:values->object){if(!v.is(Json::Type::Object))return fail(error,"KHR_interactivity value socket is invalid");const int source=Json::integer(v.get("node"),-1);if(source>=0&&static_cast<std::size_t>(source)>=i)return fail(error,"KHR_interactivity value edge references current/later node");}nodes.push_back(&n);}
        variables.clear();variable_names.clear();if(const Json::Value*array=graph->get("variables");array&&array->is(Json::Type::Array))for(std::size_t i=0u;i<array->array.size();++i){const Json::Value&v=array->array[i];if(!v.is(Json::Type::Object))return fail(error,"KHR_interactivity variable is invalid");const int type=Json::integer(v.get("type"),-1);if(type<0||static_cast<std::size_t>(type)>=types.size())return fail(error,"KHR_interactivity variable type is invalid");VariableState state;state.name=Json::stringValue(v.get("name"));state.value=fromJson(typeName(type),v.get("value"));if(!state.name.empty())variable_names[state.name]=variables.size();variables.push_back(std::move(state));}
        events.clear();event_ids.clear();if(const Json::Value*array=graph->get("events");array&&array->is(Json::Type::Array))for(const Json::Value&e:array->array){if(!e.is(Json::Type::Object))return fail(error,"KHR_interactivity event is invalid");EventDefinition event;event.id=Json::stringValue(e.get("id"));if(const Json::Value*values=e.get("values");values&&values->is(Json::Type::Object))for(const auto&[socket_name,type_value]:values->object){const int type=Json::integer(&type_value,-1);if(type<0||static_cast<std::size_t>(type)>=types.size())return fail(error,"KHR_interactivity event value type is invalid");event.values[socket_name]=static_cast<std::size_t>(type);}if(!event.id.empty())event_ids[event.id]=events.size();events.push_back(std::move(event));}
        transient_outputs.assign(nodes.size(),{});return true;
    }
};

Runtime::Runtime():impl_(new Impl){}
Runtime::~Runtime(){delete impl_;impl_=nullptr;}

bool Runtime::load(Ecs::World&world,Renderer::ModelScene::Instance&instance,std::string*error)
{
    if(!impl_)return false;reset();impl_->instance=&instance;if(instance.model==Models::INVALID_MODEL)return fail(error,"KHR_interactivity requires a valid model scene instance");const auto*extensions=Models::modelExtensions(instance.model);if(!extensions)return true;const auto found=extensions->find("KHR_interactivity");if(found==extensions->end())return true;if(!Json::parse(found->second,&impl_->extension,error))return false;if(!impl_->parseGraph(error))return false;if(!Models::Runtime::reset(instance.model,&impl_->pose,error))return false;impl_->loaded=true;impl_->activations_this_update=0u;for(std::size_t i=0u;i<impl_->nodes.size();++i)if(impl_->operation(i)=="event/onStart"&&!impl_->activate(world,i,"in",error)){reset();return false;}return true;
}

bool Runtime::update(Ecs::World&world,double delta_seconds,std::string*error)
{
    if(!impl_||!impl_->loaded)return true;if(!std::isfinite(delta_seconds)||delta_seconds<0.0)return fail(error,"KHR_interactivity update delta must be finite and non-negative");impl_->activations_this_update=0u;for(auto&outputs:impl_->transient_outputs){for(auto it=outputs.begin();it!=outputs.end();){if(!it->first.starts_with("__"))it=outputs.erase(it);else++it;}}
    if(!impl_->updateTimed(world,delta_seconds,error))return false;impl_->previous_tick_time=impl_->time_since_start;if(!impl_->first_tick)impl_->time_since_start+=delta_seconds;for(std::size_t i=0u;i<impl_->nodes.size();++i)if(impl_->operation(i)=="event/onTick"&&!impl_->activate(world,i,"in",error))return false;impl_->first_tick=false;++impl_->statistics.updates;return true;
}

bool Runtime::send(Ecs::World&world,std::string_view event_id,std::span<const EventValue>values,std::string*error)
{
    if(!impl_||!impl_->loaded)return false;Event event;event.id=std::string(event_id);event.values.assign(values.begin(),values.end());impl_->activations_this_update=0u;++impl_->statistics.events;return impl_->dispatchEvent(world,event,error);
}

bool Runtime::variable(std::size_t index,std::vector<double>*value)const
{
    if(!impl_||index>=impl_->variables.size()||!value)return false;*value=impl_->variables[index].value.data;return true;
}

bool Runtime::variable(std::string_view name,std::vector<double>*value)const
{
    if(!impl_||!value)return false;const auto found=impl_->variable_names.find(std::string(name));return found!=impl_->variable_names.end()&&variable(found->second,value);
}

bool Runtime::pollEvent(Event*event)
{
    if(!impl_||!event||impl_->emitted_events.empty())return false;*event=std::move(impl_->emitted_events.front());impl_->emitted_events.pop_front();return true;
}

void Runtime::reset()
{
    if(!impl_)return;const Limits limits=impl_->limits;*impl_=Impl{};impl_->limits=limits;
}

bool Runtime::active()const{return impl_&&impl_->loaded;}
std::string_view Runtime::graphName()const{return impl_?std::string_view(impl_->graph_name):std::string_view{};}
void Runtime::setLimits(const Limits&limits){if(impl_)impl_->limits=limits;}
const Limits&Runtime::limits()const{static const Limits defaults{};return impl_?impl_->limits:defaults;}
const Statistics&Runtime::statistics()const{static const Statistics empty{};return impl_?impl_->statistics:empty;}

} // namespace Interactivity
