#include "sim_protocol.h"

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
class JsonValue
{
  public:
    enum class Type
    {
        NULL_VALUE,
        BOOL,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT
    };

    Type mType = Type::NULL_VALUE;
    bool mBool = false;
    uint64_t mNumber = 0;
    std::string mString;
    std::vector<JsonValue> mArray;
    std::map<std::string, JsonValue> mObject;

    static JsonValue Null()
    {
        return {};
    }

    static JsonValue Bool(bool value)
    {
        JsonValue json;
        json.mType = Type::BOOL;
        json.mBool = value;
        return json;
    }

    static JsonValue Number(uint64_t value)
    {
        JsonValue json;
        json.mType = Type::NUMBER;
        json.mNumber = value;
        return json;
    }

    static JsonValue String(const std::string &value)
    {
        JsonValue json;
        json.mType = Type::STRING;
        json.mString = value;
        return json;
    }

    static JsonValue Array(std::vector<JsonValue> value)
    {
        JsonValue json;
        json.mType = Type::ARRAY;
        json.mArray = std::move(value);
        return json;
    }

    static JsonValue Object(std::map<std::string, JsonValue> value)
    {
        JsonValue json;
        json.mType = Type::OBJECT;
        json.mObject = std::move(value);
        return json;
    }

    bool IsObject() const { return mType == Type::OBJECT; }
    bool IsArray() const { return mType == Type::ARRAY; }
    bool IsString() const { return mType == Type::STRING; }
    bool IsNumber() const { return mType == Type::NUMBER; }
    bool IsBool() const { return mType == Type::BOOL; }
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string &input) : mInput(input) {}

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (mPos != mInput.size())
        {
            throw std::runtime_error("Trailing data after JSON value");
        }
        return value;
    }

  private:
    const std::string &mInput;
    size_t mPos = 0;

    void SkipWhitespace()
    {
        while (mPos < mInput.size() && std::isspace(static_cast<unsigned char>(mInput[mPos])))
        {
            mPos++;
        }
    }

    char Peek() const
    {
        if (mPos >= mInput.size())
        {
            throw std::runtime_error("Unexpected end of input");
        }
        return mInput[mPos];
    }

    char Consume()
    {
        char c = Peek();
        mPos++;
        return c;
    }

    void Expect(char expected)
    {
        char actual = Consume();
        if (actual != expected)
        {
            throw std::runtime_error("Unexpected JSON token");
        }
    }

    JsonValue ParseValue()
    {
        SkipWhitespace();
        char c = Peek();
        if (c == '{')
            return ParseObject();
        if (c == '[')
            return ParseArray();
        if (c == '"')
            return JsonValue::String(ParseString());
        if (c == 't' || c == 'f')
            return JsonValue::Bool(ParseBool());
        if (c == 'n')
        {
            ParseNull();
            return JsonValue::Null();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return JsonValue::Number(ParseNumber());
        throw std::runtime_error("Invalid JSON value");
    }

    JsonValue ParseObject()
    {
        Expect('{');
        SkipWhitespace();
        std::map<std::string, JsonValue> object;
        if (Peek() == '}')
        {
            Consume();
            return JsonValue::Object(std::move(object));
        }

        while (true)
        {
            SkipWhitespace();
            std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            object[key] = ParseValue();
            SkipWhitespace();
            char c = Consume();
            if (c == '}')
                break;
            if (c != ',')
                throw std::runtime_error("Expected ',' or '}' in object");
        }

        return JsonValue::Object(std::move(object));
    }

    JsonValue ParseArray()
    {
        Expect('[');
        SkipWhitespace();
        std::vector<JsonValue> array;
        if (Peek() == ']')
        {
            Consume();
            return JsonValue::Array(std::move(array));
        }

        while (true)
        {
            array.push_back(ParseValue());
            SkipWhitespace();
            char c = Consume();
            if (c == ']')
                break;
            if (c != ',')
                throw std::runtime_error("Expected ',' or ']' in array");
            SkipWhitespace();
        }

        return JsonValue::Array(std::move(array));
    }

    std::string ParseString()
    {
        Expect('"');
        std::string result;
        while (true)
        {
            char c = Consume();
            if (c == '"')
                break;
            if (c == '\\')
            {
                char escaped = Consume();
                switch (escaped)
                {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default:
                    throw std::runtime_error("Unsupported string escape");
                }
            }
            else
            {
                result.push_back(c);
            }
        }
        return result;
    }

    bool ParseBool()
    {
        if (mInput.compare(mPos, 4, "true") == 0)
        {
            mPos += 4;
            return true;
        }
        if (mInput.compare(mPos, 5, "false") == 0)
        {
            mPos += 5;
            return false;
        }
        throw std::runtime_error("Invalid boolean value");
    }

    void ParseNull()
    {
        if (mInput.compare(mPos, 4, "null") != 0)
        {
            throw std::runtime_error("Invalid null value");
        }
        mPos += 4;
    }

    uint64_t ParseNumber()
    {
        size_t start = mPos;
        if (mInput[mPos] == '-')
            mPos++;
        while (mPos < mInput.size() && std::isdigit(static_cast<unsigned char>(mInput[mPos])))
            mPos++;
        std::string token = mInput.substr(start, mPos - start);
        long long value = std::stoll(token);
        if (value < 0)
            throw std::runtime_error("Negative numbers are not supported in this protocol");
        return static_cast<uint64_t>(value);
    }
};

std::string EscapeJsonString(const std::string &value)
{
    std::ostringstream out;
    for (char c : value)
    {
        switch (c)
        {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            out << c;
            break;
        }
    }
    return out.str();
}

std::string SerializeJson(const JsonValue &value)
{
    std::ostringstream out;
    switch (value.mType)
    {
    case JsonValue::Type::NULL_VALUE:
        out << "null";
        break;
    case JsonValue::Type::BOOL:
        out << (value.mBool ? "true" : "false");
        break;
    case JsonValue::Type::NUMBER:
        out << value.mNumber;
        break;
    case JsonValue::Type::STRING:
        out << '"' << EscapeJsonString(value.mString) << '"';
        break;
    case JsonValue::Type::ARRAY:
        out << '[';
        for (size_t i = 0; i < value.mArray.size(); i++)
        {
            if (i > 0)
                out << ',';
            out << SerializeJson(value.mArray[i]);
        }
        out << ']';
        break;
    case JsonValue::Type::OBJECT:
        out << '{';
        {
            bool first = true;
            for (const auto &entry : value.mObject)
            {
                if (!first)
                    out << ',';
                first = false;
                out << '"' << EscapeJsonString(entry.first) << '"' << ':' << SerializeJson(entry.second);
            }
        }
        out << '}';
        break;
    }
    return out.str();
}

uint64_t RequireNumber(const JsonValue &value, const std::string &name)
{
    if (!value.IsNumber())
        throw std::runtime_error("Expected numeric field: " + name);
    return value.mNumber;
}

bool RequireBool(const JsonValue &value, const std::string &name)
{
    if (!value.IsBool())
        throw std::runtime_error("Expected boolean field: " + name);
    return value.mBool;
}

std::string RequireString(const JsonValue &value, const std::string &name)
{
    if (!value.IsString())
        throw std::runtime_error("Expected string field: " + name);
    return value.mString;
}

const JsonValue &RequireObjectField(const JsonValue &object, const std::string &name)
{
    if (!object.IsObject())
        throw std::runtime_error("Expected object");
    auto it = object.mObject.find(name);
    if (it == object.mObject.end())
        throw std::runtime_error("Missing field: " + name);
    return it->second;
}

const JsonValue *FindObjectField(const JsonValue &object, const std::string &name)
{
    if (!object.IsObject())
        return nullptr;
    auto it = object.mObject.find(name);
    if (it == object.mObject.end())
        return nullptr;
    return &it->second;
}

std::string BytesToHex(const std::vector<uint8_t> &data)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint8_t byte : data)
    {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

std::vector<uint8_t> HexToBytes(const std::string &hex)
{
    if ((hex.size() & 1U) != 0)
        throw std::runtime_error("Hex string must have even length");

    std::vector<uint8_t> data;
    data.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        unsigned int byte = 0;
        std::stringstream ss;
        ss << std::hex << hex.substr(i, 2);
        ss >> byte;
        if (ss.fail())
            throw std::runtime_error("Invalid hex data");
        data.push_back(static_cast<uint8_t>(byte));
    }
    return data;
}

std::string RunStopReasonString(RunStopReason reason)
{
    switch (reason)
    {
    case RunStopReason::COMPLETED:
        return "completed";
    case RunStopReason::CONDITION_MET:
        return "condition_met";
    case RunStopReason::WATCHPOINT_HIT:
        return "watchpoint_hit";
    case RunStopReason::TIMEOUT:
        return "timeout";
    case RunStopReason::ERROR:
        return "error";
    }
    return "error";
}

Condition ParseCondition(const JsonValue &json)
{
    Condition condition;
    std::string type = RequireString(RequireObjectField(json, "type"), "type");
    if (type == "signal_equals")
        condition.mType = Condition::Type::SIGNAL_EQUALS;
    else if (type == "signal_not_equals")
        condition.mType = Condition::Type::SIGNAL_NOT_EQUALS;
    else if (type == "cpu_pc_equals")
        condition.mType = Condition::Type::CPU_PC_EQUALS;
    else if (type == "cpu_pc_in_range")
        condition.mType = Condition::Type::CPU_PC_IN_RANGE;
    else if (type == "cpu_pc_out_of_range")
        condition.mType = Condition::Type::CPU_PC_OUT_OF_RANGE;
    else if (type == "and")
        condition.mType = Condition::Type::AND;
    else if (type == "or")
        condition.mType = Condition::Type::OR;
    else if (type == "not")
        condition.mType = Condition::Type::NOT;
    else
        throw std::runtime_error("Unknown condition type: " + type);

    if (const JsonValue *signal = FindObjectField(json, "signal"))
        condition.mSignal = RequireString(*signal, "signal");
    if (const JsonValue *value = FindObjectField(json, "value"))
        condition.mValue = RequireNumber(*value, "value");
    if (const JsonValue *value2 = FindObjectField(json, "value2"))
        condition.mValue2 = RequireNumber(*value2, "value2");
    if (const JsonValue *start = FindObjectField(json, "start"))
        condition.mValue = RequireNumber(*start, "start");
    if (const JsonValue *end = FindObjectField(json, "end"))
        condition.mValue2 = RequireNumber(*end, "end");

    if (const JsonValue *children = FindObjectField(json, "children"))
    {
        if (!children->IsArray())
            throw std::runtime_error("Condition children must be an array");
        for (const auto &child : children->mArray)
        {
            condition.mChildren.push_back(ParseCondition(child));
        }
    }

    return condition;
}

JsonValue MakeSuccessResponse(uint64_t id, const JsonValue &result)
{
    return JsonValue::Object({
        {"id", JsonValue::Number(id)},
        {"ok", JsonValue::Bool(true)},
        {"result", result},
    });
}

JsonValue MakeErrorResponse(uint64_t id, const std::string &code, const std::string &message)
{
    return JsonValue::Object({
        {"id", JsonValue::Number(id)},
        {"ok", JsonValue::Bool(false)},
        {"error", JsonValue::Object({
            {"code", JsonValue::String(code)},
            {"message", JsonValue::String(message)},
        })},
    });
}

template <typename T>
JsonValue WrapControllerResult(uint64_t id, const ControllerResult<T> &result, const JsonValue &payload)
{
    if (!result.ok)
        return MakeErrorResponse(id, result.errorCode, result.errorMessage);
    return MakeSuccessResponse(id, payload);
}

JsonValue RunResultToJson(const RunResult &result)
{
    return JsonValue::Object({
        {"reason", JsonValue::String(RunStopReasonString(result.mReason))},
        {"ticks_executed", JsonValue::Number(result.mTicksExecuted)},
        {"frames_executed", JsonValue::Number(result.mFramesExecuted)},
    });
}

JsonValue StatusToJson(const SimStatus &status)
{
    return JsonValue::Object({
        {"initialized", JsonValue::Bool(status.mInitialized)},
        {"running", JsonValue::Bool(status.mRunning)},
        {"paused", JsonValue::Bool(status.mPaused)},
        {"trace_active", JsonValue::Bool(status.mTraceActive)},
        {"headless", JsonValue::Bool(status.mHeadless)},
        {"total_ticks", JsonValue::Number(status.mTotalTicks)},
        {"game_name", JsonValue::String(status.mGameName)},
    });
}

JsonValue CpuStateToJson(const CpuState &state)
{
    std::vector<JsonValue> regs;
    regs.reserve(state.mRegisters.size());
    for (uint32_t reg : state.mRegisters)
        regs.push_back(JsonValue::Number(reg));

    return JsonValue::Object({
        {"pc", JsonValue::Number(state.mPc)},
        {"registers", JsonValue::Array(std::move(regs))},
        {"disasm", JsonValue::String(state.mDisasm)},
    });
}
} // namespace

SimProtocol::SimProtocol(SimController &controller) : mController(controller)
{
}

std::string SimProtocol::HandleLine(const std::string &line)
{
    uint64_t id = 0;

    try
    {
        JsonParser parser(line);
        JsonValue request = parser.Parse();
        if (!request.IsObject())
            throw std::runtime_error("Request must be a JSON object");

        id = RequireNumber(RequireObjectField(request, "id"), "id");
        std::string method = RequireString(RequireObjectField(request, "method"), "method");
        JsonValue params = JsonValue::Object({});
        if (const JsonValue *paramsField = FindObjectField(request, "params"))
            params = *paramsField;

        if (method == "sim.initialize")
        {
            bool headless = true;
            if (const JsonValue *headlessField = FindObjectField(params, "headless"))
                headless = RequireBool(*headlessField, "headless");
            auto result = mController.Initialize(headless);
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "sim.shutdown")
        {
            auto result = mController.Shutdown();
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "sim.status")
        {
            auto result = mController.GetStatus();
            return SerializeJson(WrapControllerResult(id, result, StatusToJson(result.value)));
        }
        if (method == "sim.load_game")
        {
            auto result = mController.LoadGame(RequireString(RequireObjectField(params, "name"), "name"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "sim.load_mra")
        {
            auto result = mController.LoadMra(RequireString(RequireObjectField(params, "path"), "path"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "sim.reset")
        {
            auto result = mController.Reset(RequireNumber(RequireObjectField(params, "cycles"), "cycles"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "sim.run_cycles")
        {
            auto result = mController.RunCycles(RequireNumber(RequireObjectField(params, "count"), "count"));
            return SerializeJson(WrapControllerResult(id, result, RunResultToJson(result.value)));
        }
        if (method == "sim.run_frames")
        {
            auto result = mController.RunFrames(RequireNumber(RequireObjectField(params, "count"), "count"));
            return SerializeJson(WrapControllerResult(id, result, RunResultToJson(result.value)));
        }
        if (method == "sim.run_until")
        {
            RunUntilRequest requestValue;
            requestValue.mCondition = ParseCondition(RequireObjectField(params, "condition"));
            if (const JsonValue *timeoutField = FindObjectField(params, "timeout_cycles"))
                requestValue.mTimeoutCycles = RequireNumber(*timeoutField, "timeout_cycles");
            auto result = mController.RunUntil(requestValue);
            return SerializeJson(WrapControllerResult(id, result, RunResultToJson(result.value)));
        }
        if (method == "cpu.get_state")
        {
            auto result = mController.GetCpuState();
            return SerializeJson(WrapControllerResult(id, result, CpuStateToJson(result.value)));
        }
        if (method == "memory.read")
        {
            auto result = mController.ReadMemory(
                RequireString(RequireObjectField(params, "region"), "region"),
                static_cast<uint32_t>(RequireNumber(RequireObjectField(params, "address"), "address")),
                static_cast<uint32_t>(RequireNumber(RequireObjectField(params, "size"), "size")));
            JsonValue payload = JsonValue::Object({
                {"region", JsonValue::String(result.value.mRegion)},
                {"address", JsonValue::Number(result.value.mAddress)},
                {"data_hex", JsonValue::String(BytesToHex(result.value.mData))},
            });
            return SerializeJson(WrapControllerResult(id, result, payload));
        }
        if (method == "memory.write")
        {
            auto result = mController.WriteMemory(
                RequireString(RequireObjectField(params, "region"), "region"),
                static_cast<uint32_t>(RequireNumber(RequireObjectField(params, "address"), "address")),
                HexToBytes(RequireString(RequireObjectField(params, "data_hex"), "data_hex")));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "memory.list_regions")
        {
            auto result = mController.ListRegions();
            std::vector<JsonValue> regions;
            for (const auto &region : result.value)
                regions.push_back(JsonValue::String(region));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Array(std::move(regions))));
        }
        if (method == "signal.read")
        {
            auto result = mController.ReadSignal(RequireString(RequireObjectField(params, "name"), "name"));
            JsonValue payload = JsonValue::Object({
                {"name", JsonValue::String(result.value.mSignal)},
                {"value", JsonValue::Number(result.value.mValue)},
            });
            return SerializeJson(WrapControllerResult(id, result, payload));
        }
        if (method == "state.list")
        {
            auto result = mController.ListStates();
            std::vector<JsonValue> states;
            for (const auto &state : result.value.mStates)
                states.push_back(JsonValue::String(state));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({{"states", JsonValue::Array(std::move(states))}})));
        }
        if (method == "state.save")
        {
            auto result = mController.SaveState(RequireString(RequireObjectField(params, "filename"), "filename"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "state.load")
        {
            auto result = mController.LoadState(RequireString(RequireObjectField(params, "filename"), "filename"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "trace.start")
        {
            int depth = 1;
            if (const JsonValue *depthField = FindObjectField(params, "depth"))
                depth = static_cast<int>(RequireNumber(*depthField, "depth"));
            auto result = mController.StartTrace(RequireString(RequireObjectField(params, "filename"), "filename"), depth);
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "trace.stop")
        {
            auto result = mController.StopTrace();
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "video.screenshot")
        {
            auto result = mController.SaveScreenshot(RequireString(RequireObjectField(params, "path"), "path"));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({{"path", JsonValue::String(result.value.mPath)}})));
        }
        if (method == "input.set_dipswitch_a")
        {
            auto result = mController.SetDipSwitchA(static_cast<uint8_t>(RequireNumber(RequireObjectField(params, "value"), "value")));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }
        if (method == "input.set_dipswitch_b")
        {
            auto result = mController.SetDipSwitchB(static_cast<uint8_t>(RequireNumber(RequireObjectField(params, "value"), "value")));
            return SerializeJson(WrapControllerResult(id, result, JsonValue::Object({})));
        }

        return SerializeJson(MakeErrorResponse(id, "unknown_method", "Unknown method: " + method));
    }
    catch (const std::exception &e)
    {
        return SerializeJson(MakeErrorResponse(id, "bad_request", e.what()));
    }
}
