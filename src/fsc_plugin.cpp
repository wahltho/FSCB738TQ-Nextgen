#include "plugin_config.h"

#if IBM
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <termios.h>
#include <sys/time.h>
#endif

#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMGraphics.h"
#include "XPLMDisplay.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPWidgetUtils.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <cctype>

#include "plugin_utils.h"

namespace {

static const char* kPluginVersion = PLUGIN_VERSION;

#if IBM
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

struct Prefs {
    bool logfileEnabled = true;
    std::string logfileName = PLUGIN_LOG_NAME;
    // FSC Throttle Quadrant
    enum class FscType {
        SemiPro,
        Pro,
        Motorized,
    };
    enum class FscParity {
        None,
        Even,
        Odd,
    };
    struct FscCalib {
        // Defaults are based on the reference XLua scripts in Documentation/
        // (B738X.FSC_throttle_{semi_pro,pro,motorized}.lua)
        int spoilersDown = 19;
        int spoilersArmed = 75;
        int spoilersMin = 100;
        int spoilersDetent = 185;
        int spoilersUp = 209;
        int throttle1Min = 43;
        int throttle1Full = 211;
        int throttle2Min = 43;
        int throttle2Full = 211;
        int reverser1Min = 12;
        int reverser1Max = 95;
        int reverser2Min = 12;
        int reverser2Max = 95;
        int flaps00 = 3;
        int flaps01 = 31;
        int flaps02 = 57;
        int flaps05 = 81;
        int flaps10 = 101;
        int flaps15 = 123;
        int flaps25 = 151;
        int flaps30 = 183;
        int flaps40 = 225;
    };
    struct FscMotorCalib {
        // Motor targets (0..255)
        int spoilersDown = 0;
        int spoilersUp = 206;
        int trimArrow02 = 93;
        int trimArrow17 = 219;
        int throttle1Min = 47;
        int throttle1Max = 210;
        int throttle2Min = 47;
        int throttle2Max = 210;
        // Trim wheel mapping (sim trim wheel -> motor arrow)
        float trimWheel02 = -0.76f;
        float trimWheel17 = 0.84f;
        // Update rate for throttle motors
        float throttleUpdateRateSec = 0.07f;
    };
    struct FscSerial {
        int baud = 115200;
        int dataBits = 8;
        int stopBits = 1;
        FscParity parity = FscParity::None;
        bool dtr = true;
        bool rts = true;
        bool xonxoff = false;
    };
    struct FscPrefs {
        bool enabled = false;
        std::string port;
        FscType type = FscType::SemiPro;
        bool speedBrakeReversed = false;
        bool fuelLeverInverted = false;
        int throttleSmoothMs = 60;
        int throttleDeadband = 1;
        float throttleSyncBand = 0.015f;
        bool debug = false;
        FscSerial serial;
        FscCalib calib;
        FscMotorCalib motor;
    } fsc;
};

bool g_pluginEnabled = false;
std::ofstream g_fileLog;
std::mutex g_logMutex;
Prefs g_prefs;

XPLMCommandRef g_cmdFscCalibStart = nullptr;
XPLMCommandRef g_cmdFscCalibNext = nullptr;
XPLMCommandRef g_cmdFscCalibCancel = nullptr;
XPLMCommandRef g_cmdReloadPrefs = nullptr;
XPLMMenuID g_menuId = nullptr;
int g_menuBaseItem = -1;
int g_menuToggleItem = -1;
XPWidgetID g_fscWindow = nullptr;
XPWidgetID g_fscPanel = nullptr;
XPWidgetID g_fscCheckEnabled = nullptr;
XPWidgetID g_fscRadioTypeSemi = nullptr;
XPWidgetID g_fscRadioTypePro = nullptr;
XPWidgetID g_fscRadioTypeMotor = nullptr;
XPWidgetID g_fscFieldPort = nullptr;
XPWidgetID g_fscFieldBaud = nullptr;
XPWidgetID g_fscFieldDataBits = nullptr;
XPWidgetID g_fscFieldStopBits = nullptr;
XPWidgetID g_fscRadioParityNone = nullptr;
XPWidgetID g_fscRadioParityEven = nullptr;
XPWidgetID g_fscRadioParityOdd = nullptr;
XPWidgetID g_fscCheckDtr = nullptr;
XPWidgetID g_fscCheckRts = nullptr;
XPWidgetID g_fscCheckXonxoff = nullptr;
XPWidgetID g_fscCheckFuelInvert = nullptr;
XPWidgetID g_fscCheckSpeedbrakeRev = nullptr;
XPWidgetID g_fscFieldThrottleSmoothMs = nullptr;
XPWidgetID g_fscFieldThrottleDeadband = nullptr;
XPWidgetID g_fscFieldThrottleSyncBand = nullptr;
XPWidgetID g_fscCheckDebug = nullptr;
XPWidgetID g_fscFieldMotorSpoilersDown = nullptr;
XPWidgetID g_fscFieldMotorSpoilersUp = nullptr;
XPWidgetID g_fscFieldMotorThrottle1Min = nullptr;
XPWidgetID g_fscFieldMotorThrottle1Max = nullptr;
XPWidgetID g_fscFieldMotorThrottle2Min = nullptr;
XPWidgetID g_fscFieldMotorThrottle2Max = nullptr;
XPWidgetID g_fscFieldMotorTrimArrow02 = nullptr;
XPWidgetID g_fscFieldMotorTrimArrow17 = nullptr;
XPWidgetID g_fscFieldMotorTrimWheel02 = nullptr;
XPWidgetID g_fscFieldMotorTrimWheel17 = nullptr;
XPWidgetID g_fscFieldMotorThrottleRate = nullptr;
XPWidgetID g_fscCalibStatus = nullptr;
XPWidgetID g_fscBtnSaveApply = nullptr;
XPWidgetID g_fscBtnReload = nullptr;
XPWidgetID g_fscBtnCalibStart = nullptr;
XPWidgetID g_fscBtnCalibNext = nullptr;
XPWidgetID g_fscBtnCalibCancel = nullptr;
XPWidgetID g_fscBtnClose = nullptr;
std::string g_fscCalibStatusText = "Calibration: idle";
static void* kMenuToggleWindow = reinterpret_cast<void*>(1);

struct DeferredInit {
    bool pending = false;
    std::chrono::steady_clock::time_point due{};
    int attempts = 0;
};

DeferredInit g_fscDeferredInit;

struct FscBindings {
    XPLMCommandRef cmdMixture1Idle{nullptr};
    XPLMCommandRef cmdMixture1Cutoff{nullptr};
    XPLMCommandRef cmdMixture2Idle{nullptr};
    XPLMCommandRef cmdMixture2Cutoff{nullptr};
    XPLMCommandRef cmdParkBrake{nullptr};
    XPLMCommandRef cmdToga{nullptr};
    XPLMCommandRef cmdAtDisengage{nullptr};
    XPLMCommandRef cmdHorn{nullptr};
    XPLMCommandRef cmdPitchTrimUp{nullptr};
    XPLMCommandRef cmdPitchTrimDown{nullptr};
    XPLMCommandRef cmdFlaps0{nullptr};
    XPLMCommandRef cmdFlaps1{nullptr};
    XPLMCommandRef cmdFlaps2{nullptr};
    XPLMCommandRef cmdFlaps5{nullptr};
    XPLMCommandRef cmdFlaps10{nullptr};
    XPLMCommandRef cmdFlaps15{nullptr};
    XPLMCommandRef cmdFlaps25{nullptr};
    XPLMCommandRef cmdFlaps30{nullptr};
    XPLMCommandRef cmdFlaps40{nullptr};
    XPLMCommandRef cmdSpeedbrakeUpOne{nullptr};
    XPLMCommandRef cmdSpeedbrakeDownOne{nullptr};
    XPLMCommandRef cmdElTrimGuardOn{nullptr};
    XPLMCommandRef cmdElTrimGuardOff{nullptr};
    XPLMCommandRef cmdApTrimGuardOn{nullptr};
    XPLMCommandRef cmdApTrimGuardOff{nullptr};
    XPLMDataRef drThrottle1{nullptr};
    XPLMDataRef drThrottle2{nullptr};
    XPLMDataRef drThrottleRatio{nullptr};
    XPLMDataRef drReverse1{nullptr};
    XPLMDataRef drReverse2{nullptr};
    XPLMDataRef drSpeedbrakeLever{nullptr};
    XPLMDataRef drParkingBrakePos{nullptr};
    XPLMDataRef drElTrimPos{nullptr};
    XPLMDataRef drApTrimPos{nullptr};
    // Motorized outputs / sim state
    XPLMDataRef drAnnParkingBrake{nullptr};
    XPLMDataRef drBacklight{nullptr};
    XPLMDataRef drOnGroundAny{nullptr};
    XPLMDataRef drSpeedbrakeRatio{nullptr};
    XPLMDataRef drThrustLever{nullptr};
    XPLMDataRef drLockThrottle{nullptr};
    XPLMDataRef drAutothrottleArmPos{nullptr};
    XPLMDataRef drTrimWheel{nullptr};
    XPLMDataRef drLeftToeBrake{nullptr};
    XPLMDataRef drRightToeBrake{nullptr};
} g_fscBindings;

struct FscState {
    int digital = -1;
    int stabTrim = -1;  // bitfield from 0x96/0x97 packet (0x02 MAIN ELEC, 0x04 AUTO PILOT)
    int trimWheelDelta = 0;  // accumulated quadrature steps since last flightloop
    int reverser1 = -1;
    int reverser2 = -1;
    int throttle1 = -1;
    int throttle2 = -1;
    int flaps = -1;
    int speedbrake = -1;
};

std::mutex g_fscMutex;
FscState g_fscState;
std::atomic<bool> g_fscRunning{false};
std::atomic<bool> g_fscResyncPending{false};
std::atomic<bool> g_fscAxisResyncPending{false};
std::chrono::steady_clock::time_point g_fscAxisResyncDue{};
std::thread g_fscThread;
std::atomic<intptr_t> g_fscFd{-1};
std::mutex g_fscIoMutex;

std::atomic<bool> g_fscMotorThrottleActive{false};
std::atomic<bool> g_fscMotorSpeedbrakeActive{false};

struct FscOutputState {
    bool parkBrakeLightKnown = false;
    bool parkBrakeLightOn = false;
    uint8_t digitalMask = 0;
    uint8_t motorPowerMask = 0;
    int motorThrottle1Pos = -1;
    int motorThrottle2Pos = -1;
    int motorSpeedbrakePos = -1;
    int motorTrimIndPos = -1;
    std::chrono::steady_clock::time_point speedbrakeMotorOffTime{};
    std::chrono::steady_clock::time_point trimIndMotorOffTime{};
    std::chrono::steady_clock::time_point lastThrottleUpdate{};
    float lastTrimWheel = std::numeric_limits<float>::quiet_NaN();
};

FscOutputState g_fscOut;

struct FscPrev {
    int fuel1 = -1;
    int fuel2 = -1;
    int toga = -1;
    int at = -1;
    int park = -1;
    int horn = -1;
    int flaps = -1;
    int speedbrakeState = -1;
    int speedbrakePrev = -1;
} g_fscPrev;

enum class FscCalibStep {
    SpeedbrakeDown,
    SpeedbrakeArmed,
    SpeedbrakeMin,
    SpeedbrakeDetent,
    SpeedbrakeUp,
    Throttle1Min,
    Throttle1Full,
    Throttle2Min,
    Throttle2Full,
    Reverser1Min,
    Reverser1Max,
    Reverser2Min,
    Reverser2Max,
    Flaps00,
    Flaps01,
    Flaps02,
    Flaps05,
    Flaps10,
    Flaps15,
    Flaps25,
    Flaps30,
    Flaps40,
};

template <size_t N>
struct IntRingBuffer {
    std::array<int, N> data{};
    size_t size = 0;
    size_t head = 0;

    void clear() {
        size = 0;
        head = 0;
    }

    void push(int v) {
        data[head] = v;
        head = (head + 1) % N;
        if (size < N) {
            ++size;
        }
    }

    struct Stats {
        int min = 0;
        int max = 0;
        int median = 0;
        size_t count = 0;
    };

    std::optional<Stats> stats() const {
        if (size == 0) {
            return std::nullopt;
        }
        std::vector<int> tmp;
        tmp.reserve(size);
        // Oldest→newest
        size_t start = (head + N - size) % N;
        for (size_t i = 0; i < size; ++i) {
            tmp.push_back(data[(start + i) % N]);
        }
        std::sort(tmp.begin(), tmp.end());
        Stats s{};
        s.count = tmp.size();
        s.min = tmp.front();
        s.max = tmp.back();
        s.median = tmp[tmp.size() / 2];
        return s;
    }
};

struct FscCalibWizard {
    bool active = false;
    bool safeOutputsSent = false;
    Prefs::FscType type = Prefs::FscType::SemiPro;
    size_t stepIndex = 0;
    std::vector<FscCalibStep> steps;

    // Raw captures (speedbrake may be normalized later based on direction).
    int sbDownRaw = -1;
    int sbArmedRaw = -1;
    int sbMinRaw = -1;
    int sbDetentRaw = -1;
    int sbUpRaw = -1;

    int throttle1Min = -1;
    int throttle1Full = -1;
    int throttle2Min = -1;
    int throttle2Full = -1;
    int reverser1Min = -1;
    int reverser1Max = -1;
    int reverser2Min = -1;
    int reverser2Max = -1;

    // SEMIPRO only: raw detents for flaps
    int flaps00 = -1;
    int flaps01 = -1;
    int flaps02 = -1;
    int flaps05 = -1;
    int flaps10 = -1;
    int flaps15 = -1;
    int flaps25 = -1;
    int flaps30 = -1;
    int flaps40 = -1;

    IntRingBuffer<64> histSpeedbrake;
    IntRingBuffer<64> histThrottle1;
    IntRingBuffer<64> histThrottle2;
    IntRingBuffer<64> histReverser1;
    IntRingBuffer<64> histReverser2;
    IntRingBuffer<64> histFlaps;

    void reset(Prefs::FscType t) {
        active = true;
        safeOutputsSent = false;
        type = t;
        stepIndex = 0;
        steps.clear();
        sbDownRaw = sbArmedRaw = sbMinRaw = sbDetentRaw = sbUpRaw = -1;
        throttle1Min = throttle1Full = throttle2Min = throttle2Full = -1;
        reverser1Min = reverser1Max = reverser2Min = reverser2Max = -1;
        flaps00 = flaps01 = flaps02 = flaps05 = flaps10 = flaps15 = flaps25 = flaps30 = flaps40 = -1;
        histSpeedbrake.clear();
        histThrottle1.clear();
        histThrottle2.clear();
        histReverser1.clear();
        histReverser2.clear();
        histFlaps.clear();

        steps = {
            FscCalibStep::SpeedbrakeDown,
            FscCalibStep::SpeedbrakeArmed,
            FscCalibStep::SpeedbrakeMin,
            FscCalibStep::SpeedbrakeDetent,
            FscCalibStep::SpeedbrakeUp,
            FscCalibStep::Throttle1Min,
            FscCalibStep::Throttle1Full,
            FscCalibStep::Throttle2Min,
            FscCalibStep::Throttle2Full,
            FscCalibStep::Reverser1Min,
            FscCalibStep::Reverser1Max,
            FscCalibStep::Reverser2Min,
            FscCalibStep::Reverser2Max,
        };
        if (t == Prefs::FscType::SemiPro) {
            const FscCalibStep flapSteps[] = {
                FscCalibStep::Flaps00, FscCalibStep::Flaps01, FscCalibStep::Flaps02, FscCalibStep::Flaps05,
                FscCalibStep::Flaps10, FscCalibStep::Flaps15, FscCalibStep::Flaps25, FscCalibStep::Flaps30,
                FscCalibStep::Flaps40,
            };
            steps.insert(steps.end(), std::begin(flapSteps), std::end(flapSteps));
        }
    }
};

std::mutex g_fscCalibMutex;
FscCalibWizard g_fscCalib;

void processFscState(const FscState& state);
void processFscOutputs(const FscState& inputState);
void updateFscCalibration(const FscState& inputState);
bool fscIsConnected();
static bool resyncFscLatchingInputs(const FscState& state);
static void scheduleFscAxisResync();
static bool resyncFscDetentAxes();
std::string fscCalibPrompt(FscCalibStep step, Prefs::FscType type);
static bool writeFscSettingsToPrefsFile(const Prefs& prefs);
static void reloadPrefs();
void logLine(const std::string& msg);
static void updateFscLifecycle(const char* reason);

std::string getPrefsPath() {
    char sysPath[512]{};
    XPLMGetSystemPath(sysPath);
    std::string base(sysPath);
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base.push_back('/');
    }
    return base + "Output/preferences/" + std::string(PLUGIN_PREFS_FILE);
}

std::string makePluginPath(const std::string& relative) {
    char sysPath[512]{};
    XPLMGetSystemPath(sysPath);
    std::string base(sysPath);
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base.push_back('/');
    }
    return base + relative;
}

static void parseFscType(const std::string& val, Prefs::FscType& out) {
    std::string s;
    s.reserve(val.size());
    for (char c : val) {
        s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (s == "SEMIPRO" || s == "SEMI_PRO" || s == "SEMI-PRO") {
        out = Prefs::FscType::SemiPro;
    } else if (s == "SEMIPERO") {  // common typo
        out = Prefs::FscType::SemiPro;
    } else if (s == "PRO") {
        out = Prefs::FscType::Pro;
    } else if (s == "MOTORIZED" || s == "MOTORISED") {
        out = Prefs::FscType::Motorized;
    }
}

static void parseFscParity(const std::string& val, Prefs::FscParity& out) {
    std::string s;
    s.reserve(val.size());
    for (char c : val) {
        s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (s == "N" || s == "NONE" || s == "NO") {
        out = Prefs::FscParity::None;
    } else if (s == "E" || s == "EVEN") {
        out = Prefs::FscParity::Even;
    } else if (s == "O" || s == "ODD") {
        out = Prefs::FscParity::Odd;
    }
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, std::shared_ptr<JsonValue>> objectValue;
};

static std::string jsonTypeName(JsonValue::Type type) {
    switch (type) {
        case JsonValue::Type::Null: return "null";
        case JsonValue::Type::Bool: return "bool";
        case JsonValue::Type::Number: return "number";
        case JsonValue::Type::String: return "string";
        case JsonValue::Type::Array: return "array";
        case JsonValue::Type::Object: return "object";
    }
    return "unknown";
}

static const JsonValue* jsonGet(const JsonValue& obj, const std::string& key) {
    if (obj.type != JsonValue::Type::Object) {
        return nullptr;
    }
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end()) {
        return nullptr;
    }
    if (!it->second) {
        return nullptr;
    }
    return it->second.get();
}

static bool parseJson(const std::string& text, JsonValue& out, std::string& err) {
    size_t i = 0;
    auto skipWs = [&]() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    };
    std::function<bool(JsonValue&)> parseValue;

    auto parseString = [&](std::string& outStr) -> bool {
        if (i >= text.size() || text[i] != '"') {
            err = "Expected string";
            return false;
        }
        ++i;
        std::string s;
        while (i < text.size()) {
            char c = text[i++];
            if (c == '"') {
                outStr = s;
                return true;
            }
            if (c == '\\') {
                if (i >= text.size()) {
                    err = "Invalid escape";
                    return false;
                }
                char esc = text[i++];
                switch (esc) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/': s.push_back('/'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    case 'u':
                        err = "Unicode escapes are not supported";
                        return false;
                    default:
                        err = "Invalid escape";
                        return false;
                }
            } else {
                s.push_back(c);
            }
        }
        err = "Unterminated string";
        return false;
    };

    auto parseNumber = [&](double& outNum) -> bool {
        const char* start = text.c_str() + i;
        char* end = nullptr;
        outNum = std::strtod(start, &end);
        if (end == start) {
            err = "Invalid number";
            return false;
        }
        i += static_cast<size_t>(end - start);
        return true;
    };

    auto parseArray = [&](JsonValue& outVal) -> bool {
        if (i >= text.size() || text[i] != '[') {
            err = "Expected array";
            return false;
        }
        ++i;
        skipWs();
        outVal.type = JsonValue::Type::Array;
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }
        while (i < text.size()) {
            JsonValue elem;
            if (!parseValue(elem)) {
                return false;
            }
            outVal.arrayValue.push_back(std::move(elem));
            skipWs();
            if (i < text.size() && text[i] == ',') {
                ++i;
                skipWs();
                continue;
            }
            if (i < text.size() && text[i] == ']') {
                ++i;
                return true;
            }
            err = "Expected ',' or ']'";
            return false;
        }
        err = "Unterminated array";
        return false;
    };

    auto parseObject = [&](JsonValue& outVal) -> bool {
        if (i >= text.size() || text[i] != '{') {
            err = "Expected object";
            return false;
        }
        ++i;
        skipWs();
        outVal.type = JsonValue::Type::Object;
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        while (i < text.size()) {
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWs();
            if (i >= text.size() || text[i] != ':') {
                err = "Expected ':'";
                return false;
            }
            ++i;
            skipWs();
            auto value = std::make_shared<JsonValue>();
            if (!parseValue(*value)) {
                return false;
            }
            if (outVal.objectValue.find(key) != outVal.objectValue.end()) {
                err = "Duplicate key: " + key;
                return false;
            }
            outVal.objectValue.emplace(std::move(key), std::move(value));
            skipWs();
            if (i < text.size() && text[i] == ',') {
                ++i;
                skipWs();
                continue;
            }
            if (i < text.size() && text[i] == '}') {
                ++i;
                return true;
            }
            err = "Expected ',' or '}'";
            return false;
        }
        err = "Unterminated object";
        return false;
    };

    parseValue = [&](JsonValue& outVal) -> bool {
        skipWs();
        if (i >= text.size()) {
            err = "Unexpected end of input";
            return false;
        }
        char c = text[i];
        if (c == '"') {
            outVal.type = JsonValue::Type::String;
            return parseString(outVal.stringValue);
        }
        if (c == '{') {
            return parseObject(outVal);
        }
        if (c == '[') {
            return parseArray(outVal);
        }
        if (c == 't' && text.compare(i, 4, "true") == 0) {
            i += 4;
            outVal.type = JsonValue::Type::Bool;
            outVal.boolValue = true;
            return true;
        }
        if (c == 'f' && text.compare(i, 5, "false") == 0) {
            i += 5;
            outVal.type = JsonValue::Type::Bool;
            outVal.boolValue = false;
            return true;
        }
        if (c == 'n' && text.compare(i, 4, "null") == 0) {
            i += 4;
            outVal.type = JsonValue::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            outVal.type = JsonValue::Type::Number;
            return parseNumber(outVal.numberValue);
        }
        err = "Unexpected token";
        return false;
    };

    if (!parseValue(out)) {
        return false;
    }
    skipWs();
    if (i != text.size()) {
        err = "Trailing characters after JSON value";
        return false;
    }
    return true;
}

enum class FscAxisId { Throttle1, Throttle2, Reverser1, Reverser2, Speedbrake, Flaps, Count };
enum class FscSwitchId {
    FuelCutoff1,
    FuelCutoff2,
    ParkingBrake,
    TogaLeft,
    AutothrottleDisengage,
    GearHornCutout,
    PitchTrimWheel,
    ElTrimGuard,
    ApTrimGuard,
    Count
};
enum class FscIndicatorId { ParkingBrakeLight, Backlight, Count };

struct FscAction {
    enum class Type { Command, Dataref };
    enum class Phase { Once, Begin, End };
    Type type = Type::Command;
    Phase phase = Phase::Once;
    std::string path;
    XPLMCommandRef cmd = nullptr;
    XPLMDataRef dataref = nullptr;
    int datarefType = 0;
    int index = -1;
    float value = 0.0f;
};

struct FscAxisTarget {
    std::string path;
    XPLMDataRef dataref = nullptr;
    int datarefType = 0;
    int index = -1;
};

struct FscAxisMapping {
    bool defined = false;
    std::string sourceRefMin;
    std::string sourceRefMax;
    bool invert = false;
    std::string invertRef;
    float targetMin = 0.0f;
    float targetMax = 1.0f;
    std::vector<FscAxisTarget> targets;
};

struct FscSwitchMapping {
    enum class Type { Latching, LatchingToggle, Momentary, Encoder };
    bool defined = false;
    Type type = Type::Latching;
    std::vector<FscAction> onActions;
    std::vector<FscAction> offActions;
    std::vector<FscAction> pressActions;
    std::vector<FscAction> releaseActions;
    std::vector<FscAction> cwActions;
    std::vector<FscAction> ccwActions;
    std::string stateDatarefPath;
    XPLMDataRef stateDataref = nullptr;
    int stateDatarefType = 0;
    float stateOnMin = 0.5f;
};

struct FscIndicatorMapping {
    bool defined = false;
    std::string datarefPath;
    XPLMDataRef dataref = nullptr;
    int datarefType = 0;
    float onMin = 0.5f;
    bool invert = false;
};

struct FscSpeedbrakeDetent {
    std::string sourceRef;
    int tolerance = 2;
    std::vector<FscAction> actions;
    std::vector<FscAction> actionsIfRatioZero;
    std::vector<FscAction> actionsIfRatioNonzero;
    bool hasConditional = false;
    bool hasConfirm = false;
    float confirmValue = 0.0f;
};

struct FscSpeedbrakeAnalogTarget {
    std::string path;
    XPLMDataRef dataref = nullptr;
    int datarefType = 0;
    int index = -1;
    float targetMin = 0.0f;
    float targetMax = 1.0f;
};

struct FscSpeedbrakeBehavior {
    bool enabled = false;
    std::string invertRef;
    std::string ratioDatarefPath;
    XPLMDataRef ratioDataref = nullptr;
    int ratioDatarefType = 0;
    FscSpeedbrakeDetent detentDown;
    FscSpeedbrakeDetent detentArmed;
    FscSpeedbrakeDetent detentUp;
    std::string sourceRefMin;
    std::string sourceRefMax;
    std::vector<FscSpeedbrakeAnalogTarget> analogTargets;
};

struct FscFlapsPosition {
    std::string name;
    std::string sourceRef;
    int tolerance = 2;
    std::vector<FscAction> actions;
};

struct FscFlapsBehavior {
    bool enabled = false;
    bool modeNearest = true;
    std::vector<FscFlapsPosition> positions;
};

struct FscMotorizedThrottleFollow {
    std::string lockDatarefPath;
    std::string armDatarefPath;
    std::string leverDatarefPath;
    XPLMDataRef lockDataref = nullptr;
    XPLMDataRef armDataref = nullptr;
    XPLMDataRef leverDataref = nullptr;
    int lockDatarefType = 0;
    int armDatarefType = 0;
    int leverDatarefType = 0;
    std::string thr1MinRef;
    std::string thr1MaxRef;
    std::string thr2MinRef;
    std::string thr2MaxRef;
    std::string updateRateRef;
};

struct FscMotorizedSpeedbrake {
    std::string ratioDatarefPath;
    XPLMDataRef ratioDataref = nullptr;
    int ratioDatarefType = 0;
    std::string armRef;
    std::string upRef;
    int tolerance = 3;
    float ratioUpMin = 0.99f;
    float ratioDownMax = 0.01f;
    std::string motorDownRef;
    std::string motorUpRef;
    int holdMs = 1500;
};

struct FscMotorizedTrimIndicator {
    std::string wheelDatarefPath;
    XPLMDataRef wheelDataref = nullptr;
    int wheelDatarefType = 0;
    std::string wheelMinRef;
    std::string wheelMaxRef;
    std::string arrowMinRef;
    std::string arrowMaxRef;
    int holdMs = 1500;
};

struct FscMotorizedBehavior {
    bool enabled = false;
    FscMotorizedThrottleFollow throttleFollow;
    FscMotorizedSpeedbrake speedbrake;
    FscMotorizedTrimIndicator trimIndicator;
    std::string onGroundDatarefPath;
    XPLMDataRef onGroundDataref = nullptr;
    int onGroundDatarefType = 0;
    std::string leftToeBrakeDatarefPath;
    XPLMDataRef leftToeBrakeDataref = nullptr;
    int leftToeBrakeDatarefType = 0;
    std::string rightToeBrakeDatarefPath;
    XPLMDataRef rightToeBrakeDataref = nullptr;
    int rightToeBrakeDatarefType = 0;
};

struct FscSyncSettings {
    bool deferUntilDatarefs = true;
    int startupDelaySec = 10;
    bool resyncOnAircraftLoaded = true;
    float resyncIntervalSec = 1.0f;
};

struct FscProfileRuntime {
    std::string profileId;
    std::string name;
    double version = 0.0;
    std::vector<std::string> tailnums;
    std::array<FscAxisMapping, static_cast<size_t>(FscAxisId::Count)> axes{};
    std::array<FscSwitchMapping, static_cast<size_t>(FscSwitchId::Count)> switches{};
    std::array<FscIndicatorMapping, static_cast<size_t>(FscIndicatorId::Count)> indicators{};
    FscSpeedbrakeBehavior speedbrake{};
    FscFlapsBehavior flaps{};
    FscMotorizedBehavior motorized{};
    FscSyncSettings sync{};
};

struct FscSwitchState {
    bool known = false;
    bool value = false;
};

struct FscProfileRecord {
    std::string path;
    FscProfileRuntime runtime;
};

static FscProfileRuntime g_fscProfileRuntime{};
static std::array<FscSwitchState, static_cast<size_t>(FscSwitchId::Count)> g_fscSwitchState{};
static std::atomic<bool> g_fscProfileActive{false};
static std::atomic<bool> g_fscProfileValid{false};
static std::string g_fscProfileId;
static std::string g_fscActiveProfileId;
static std::string g_fscProfilePath;
static std::vector<FscProfileRecord> g_fscProfiles;
static std::chrono::steady_clock::time_point g_fscLastResync{};

static bool getPrefIntByKey(const std::string& key, int& out) {
    const auto& c = g_prefs.fsc.calib;
    const auto& m = g_prefs.fsc.motor;
    if (key == "fsc.calib.throttle1_min") { out = c.throttle1Min; return true; }
    if (key == "fsc.calib.throttle1_full") { out = c.throttle1Full; return true; }
    if (key == "fsc.calib.throttle2_min") { out = c.throttle2Min; return true; }
    if (key == "fsc.calib.throttle2_full") { out = c.throttle2Full; return true; }
    if (key == "fsc.calib.reverser1_min") { out = c.reverser1Min; return true; }
    if (key == "fsc.calib.reverser1_max") { out = c.reverser1Max; return true; }
    if (key == "fsc.calib.reverser2_min") { out = c.reverser2Min; return true; }
    if (key == "fsc.calib.reverser2_max") { out = c.reverser2Max; return true; }
    if (key == "fsc.calib.spoilers_down") { out = c.spoilersDown; return true; }
    if (key == "fsc.calib.spoilers_armed") { out = c.spoilersArmed; return true; }
    if (key == "fsc.calib.spoilers_min") { out = c.spoilersMin; return true; }
    if (key == "fsc.calib.spoilers_detent") { out = c.spoilersDetent; return true; }
    if (key == "fsc.calib.spoilers_up") { out = c.spoilersUp; return true; }
    if (key == "fsc.calib.flaps_00") { out = c.flaps00; return true; }
    if (key == "fsc.calib.flaps_01") { out = c.flaps01; return true; }
    if (key == "fsc.calib.flaps_02") { out = c.flaps02; return true; }
    if (key == "fsc.calib.flaps_05") { out = c.flaps05; return true; }
    if (key == "fsc.calib.flaps_10") { out = c.flaps10; return true; }
    if (key == "fsc.calib.flaps_15") { out = c.flaps15; return true; }
    if (key == "fsc.calib.flaps_25") { out = c.flaps25; return true; }
    if (key == "fsc.calib.flaps_30") { out = c.flaps30; return true; }
    if (key == "fsc.calib.flaps_40") { out = c.flaps40; return true; }
    if (key == "fsc.motor.spoilers_down") { out = m.spoilersDown; return true; }
    if (key == "fsc.motor.spoilers_up") { out = m.spoilersUp; return true; }
    if (key == "fsc.motor.throttle1_min") { out = m.throttle1Min; return true; }
    if (key == "fsc.motor.throttle1_max") { out = m.throttle1Max; return true; }
    if (key == "fsc.motor.throttle2_min") { out = m.throttle2Min; return true; }
    if (key == "fsc.motor.throttle2_max") { out = m.throttle2Max; return true; }
    if (key == "fsc.motor.trim_arrow_02") { out = m.trimArrow02; return true; }
    if (key == "fsc.motor.trim_arrow_17") { out = m.trimArrow17; return true; }
    return false;
}

static bool getPrefFloatByKey(const std::string& key, float& out) {
    const auto& m = g_prefs.fsc.motor;
    if (key == "fsc.motor.trim_wheel_02") { out = m.trimWheel02; return true; }
    if (key == "fsc.motor.trim_wheel_17") { out = m.trimWheel17; return true; }
    if (key == "fsc.motor.throttle_update_rate_sec") { out = m.throttleUpdateRateSec; return true; }
    return false;
}

static bool getPrefBoolByKey(const std::string& key, bool& out) {
    if (key == "fsc.speed_brake_reversed") { out = g_prefs.fsc.speedBrakeReversed; return true; }
    if (key == "fsc.fuel_lever_inverted") { out = g_prefs.fsc.fuelLeverInverted; return true; }
    return false;
}

static std::optional<FscAxisId> axisIdFromString(const std::string& s) {
    if (s == "throttle1") return FscAxisId::Throttle1;
    if (s == "throttle2") return FscAxisId::Throttle2;
    if (s == "reverser1") return FscAxisId::Reverser1;
    if (s == "reverser2") return FscAxisId::Reverser2;
    if (s == "speedbrake") return FscAxisId::Speedbrake;
    if (s == "flaps") return FscAxisId::Flaps;
    return std::nullopt;
}

static std::optional<FscSwitchId> switchIdFromString(const std::string& s) {
    if (s == "fuel_cutoff_1") return FscSwitchId::FuelCutoff1;
    if (s == "fuel_cutoff_2") return FscSwitchId::FuelCutoff2;
    if (s == "parking_brake") return FscSwitchId::ParkingBrake;
    if (s == "toga_left") return FscSwitchId::TogaLeft;
    if (s == "autothrottle_disengage") return FscSwitchId::AutothrottleDisengage;
    if (s == "gear_horn_cutout") return FscSwitchId::GearHornCutout;
    if (s == "pitch_trim_wheel") return FscSwitchId::PitchTrimWheel;
    if (s == "el_trim_guard") return FscSwitchId::ElTrimGuard;
    if (s == "ap_trim_guard") return FscSwitchId::ApTrimGuard;
    return std::nullopt;
}

static std::optional<FscIndicatorId> indicatorIdFromString(const std::string& s) {
    if (s == "parking_brake_light") return FscIndicatorId::ParkingBrakeLight;
    if (s == "backlight") return FscIndicatorId::Backlight;
    return std::nullopt;
}

static const char* axisIdToString(FscAxisId id) {
    switch (id) {
        case FscAxisId::Throttle1: return "throttle1";
        case FscAxisId::Throttle2: return "throttle2";
        case FscAxisId::Reverser1: return "reverser1";
        case FscAxisId::Reverser2: return "reverser2";
        case FscAxisId::Speedbrake: return "speedbrake";
        case FscAxisId::Flaps: return "flaps";
        case FscAxisId::Count: break;
    }
    return "unknown";
}

static const char* switchIdToString(FscSwitchId id) {
    switch (id) {
        case FscSwitchId::FuelCutoff1: return "fuel_cutoff_1";
        case FscSwitchId::FuelCutoff2: return "fuel_cutoff_2";
        case FscSwitchId::ParkingBrake: return "parking_brake";
        case FscSwitchId::TogaLeft: return "toga_left";
        case FscSwitchId::AutothrottleDisengage: return "autothrottle_disengage";
        case FscSwitchId::GearHornCutout: return "gear_horn_cutout";
        case FscSwitchId::PitchTrimWheel: return "pitch_trim_wheel";
        case FscSwitchId::ElTrimGuard: return "el_trim_guard";
        case FscSwitchId::ApTrimGuard: return "ap_trim_guard";
        case FscSwitchId::Count: break;
    }
    return "unknown";
}

static const char* indicatorIdToString(FscIndicatorId id) {
    switch (id) {
        case FscIndicatorId::ParkingBrakeLight: return "parking_brake_light";
        case FscIndicatorId::Backlight: return "backlight";
        case FscIndicatorId::Count: break;
    }
    return "unknown";
}

static const char* switchTypeToString(FscSwitchMapping::Type type) {
    switch (type) {
        case FscSwitchMapping::Type::Latching: return "latching";
        case FscSwitchMapping::Type::LatchingToggle: return "latching_toggle";
        case FscSwitchMapping::Type::Momentary: return "momentary";
        case FscSwitchMapping::Type::Encoder: return "encoder";
    }
    return "unknown";
}

static const char* actionPhaseToString(FscAction::Phase phase) {
    switch (phase) {
        case FscAction::Phase::Once: return "once";
        case FscAction::Phase::Begin: return "begin";
        case FscAction::Phase::End: return "end";
    }
    return "unknown";
}

static const char* actionTypeToString(FscAction::Type type) {
    return type == FscAction::Type::Command ? "command" : "dataref";
}

static std::string joinStrings(const std::vector<std::string>& items, const char* sep) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += sep;
        }
        out += item;
    }
    return out;
}

static std::string formatPathIndex(const std::string& path, int index) {
    if (index >= 0) {
        return path + "[" + std::to_string(index) + "]";
    }
    return path;
}

static std::string describeAction(const FscAction& action) {
    std::string out = std::string(actionTypeToString(action.type)) + " path=" + action.path;
    if (action.type == FscAction::Type::Command) {
        out += " phase=";
        out += actionPhaseToString(action.phase);
    } else {
        out += " value=" + std::to_string(action.value);
        if (action.index >= 0) {
            out += " index=" + std::to_string(action.index);
        }
    }
    return out;
}

static void logFscActionList(const std::string& prefix, const std::vector<FscAction>& actions) {
    for (size_t i = 0; i < actions.size(); ++i) {
        logLine(prefix + "[" + std::to_string(i) + "]: " + describeAction(actions[i]));
    }
}

static void logFscProfileDetails(const FscProfileRuntime& profile) {
    logLine("FSC: profile sync: defer_until_datarefs=" + bool01(profile.sync.deferUntilDatarefs) +
            ", startup_delay_sec=" + std::to_string(profile.sync.startupDelaySec) +
            ", resync_on_aircraft_loaded=" + bool01(profile.sync.resyncOnAircraftLoaded) +
            ", resync_interval_sec=" + std::to_string(profile.sync.resyncIntervalSec));

    for (size_t i = 0; i < profile.axes.size(); ++i) {
        const auto& axis = profile.axes[i];
        if (!axis.defined) {
            continue;
        }
        std::string invert;
        if (!axis.invertRef.empty()) {
            invert = "invert_ref=" + axis.invertRef;
        } else {
            invert = "invert=" + bool01(axis.invert);
        }
        std::string targets;
        for (const auto& target : axis.targets) {
            if (!targets.empty()) {
                targets += ",";
            }
            targets += formatPathIndex(target.path, target.index);
        }
        if (targets.empty()) {
            targets = "none";
        }
        logLine("FSC: profile axis " + std::string(axisIdToString(static_cast<FscAxisId>(i))) +
                ": source_ref_min=" + axis.sourceRefMin +
                ", source_ref_max=" + axis.sourceRefMax +
                ", " + invert +
                ", target_range=[" + std::to_string(axis.targetMin) + "," + std::to_string(axis.targetMax) + "]" +
                ", targets=" + targets);
    }

    for (size_t i = 0; i < profile.switches.size(); ++i) {
        const auto& sw = profile.switches[i];
        if (!sw.defined) {
            continue;
        }
        logLine("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                ": type=" + switchTypeToString(sw.type) +
                ", state_dataref=" + (sw.stateDatarefPath.empty() ? "none" : sw.stateDatarefPath) +
                ", state_on_min=" + std::to_string(sw.stateOnMin) +
                ", on_actions=" + std::to_string(sw.onActions.size()) +
                ", off_actions=" + std::to_string(sw.offActions.size()) +
                ", press_actions=" + std::to_string(sw.pressActions.size()) +
                ", release_actions=" + std::to_string(sw.releaseActions.size()) +
                ", cw_actions=" + std::to_string(sw.cwActions.size()) +
                ", ccw_actions=" + std::to_string(sw.ccwActions.size()));
        if (!sw.onActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " on_action",
                             sw.onActions);
        }
        if (!sw.offActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " off_action",
                             sw.offActions);
        }
        if (!sw.pressActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " press_action",
                             sw.pressActions);
        }
        if (!sw.releaseActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " release_action",
                             sw.releaseActions);
        }
        if (!sw.cwActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " cw_action",
                             sw.cwActions);
        }
        if (!sw.ccwActions.empty()) {
            logFscActionList("FSC: profile switch " + std::string(switchIdToString(static_cast<FscSwitchId>(i))) +
                                 " ccw_action",
                             sw.ccwActions);
        }
    }

    for (size_t i = 0; i < profile.indicators.size(); ++i) {
        const auto& indicator = profile.indicators[i];
        if (!indicator.defined) {
            continue;
        }
        logLine("FSC: profile indicator " + std::string(indicatorIdToString(static_cast<FscIndicatorId>(i))) +
                ": dataref=" + indicator.datarefPath +
                ", on_min=" + std::to_string(indicator.onMin) +
                ", invert=" + bool01(indicator.invert));
    }

    if (profile.speedbrake.enabled) {
        const auto& sb = profile.speedbrake;
        logLine("FSC: profile speedbrake: invert_ref=" + (sb.invertRef.empty() ? "none" : sb.invertRef) +
                ", ratio_dataref=" + (sb.ratioDatarefPath.empty() ? "none" : sb.ratioDatarefPath) +
                ", source_ref_min=" + sb.sourceRefMin +
                ", source_ref_max=" + sb.sourceRefMax +
                ", analog_targets=" + std::to_string(sb.analogTargets.size()));
        auto logDetent = [&](const char* name, const FscSpeedbrakeDetent& detent) {
            logLine("FSC: profile speedbrake detent " + std::string(name) +
                    ": source_ref=" + detent.sourceRef +
                    ", tolerance=" + std::to_string(detent.tolerance) +
                    ", actions=" + std::to_string(detent.actions.size()) +
                    ", actions_if_ratio_zero=" + std::to_string(detent.actionsIfRatioZero.size()) +
                    ", actions_if_ratio_nonzero=" + std::to_string(detent.actionsIfRatioNonzero.size()) +
                    ", conditional=" + bool01(detent.hasConditional) +
                    (detent.hasConfirm ? ", confirm_value=" + std::to_string(detent.confirmValue) : ""));
            if (!detent.actions.empty()) {
                logFscActionList("FSC: profile speedbrake detent " + std::string(name) + " action", detent.actions);
            }
            if (!detent.actionsIfRatioZero.empty()) {
                logFscActionList("FSC: profile speedbrake detent " + std::string(name) + " action_if_ratio_zero",
                                 detent.actionsIfRatioZero);
            }
            if (!detent.actionsIfRatioNonzero.empty()) {
                logFscActionList("FSC: profile speedbrake detent " + std::string(name) + " action_if_ratio_nonzero",
                                 detent.actionsIfRatioNonzero);
            }
        };
        logDetent("down", sb.detentDown);
        logDetent("armed", sb.detentArmed);
        logDetent("up", sb.detentUp);
        for (const auto& target : sb.analogTargets) {
            logLine("FSC: profile speedbrake analog target: " + formatPathIndex(target.path, target.index) +
                    " range=[" + std::to_string(target.targetMin) + "," + std::to_string(target.targetMax) + "]");
        }
    } else {
        logLine("FSC: profile speedbrake: disabled");
    }

    if (profile.flaps.enabled) {
        logLine("FSC: profile flaps: mode=" + std::string(profile.flaps.modeNearest ? "nearest" : "exact") +
                ", positions=" + std::to_string(profile.flaps.positions.size()));
        for (const auto& pos : profile.flaps.positions) {
            logLine("FSC: profile flaps position " + pos.name +
                    ": source_ref=" + pos.sourceRef +
                    ", tolerance=" + std::to_string(pos.tolerance) +
                    ", actions=" + std::to_string(pos.actions.size()));
            if (!pos.actions.empty()) {
                logFscActionList("FSC: profile flaps position " + pos.name + " action", pos.actions);
            }
        }
    } else {
        logLine("FSC: profile flaps: disabled");
    }

    logLine("FSC: profile motorized: enabled=" + bool01(profile.motorized.enabled));
    if (profile.motorized.enabled) {
        const auto& motor = profile.motorized;
        logLine("FSC: profile motor throttle_follow: lock_dataref=" + motor.throttleFollow.lockDatarefPath +
                ", arm_dataref=" + motor.throttleFollow.armDatarefPath +
                ", lever_dataref=" + motor.throttleFollow.leverDatarefPath +
                ", thr1_min_ref=" + motor.throttleFollow.thr1MinRef +
                ", thr1_max_ref=" + motor.throttleFollow.thr1MaxRef +
                ", thr2_min_ref=" + motor.throttleFollow.thr2MinRef +
                ", thr2_max_ref=" + motor.throttleFollow.thr2MaxRef +
                ", update_rate_ref=" + motor.throttleFollow.updateRateRef);
        logLine("FSC: profile motor speedbrake: ratio_dataref=" + motor.speedbrake.ratioDatarefPath +
                ", arm_ref=" + motor.speedbrake.armRef +
                ", up_ref=" + motor.speedbrake.upRef +
                ", motor_down_ref=" + motor.speedbrake.motorDownRef +
                ", motor_up_ref=" + motor.speedbrake.motorUpRef +
                ", tolerance=" + std::to_string(motor.speedbrake.tolerance) +
                ", ratio_up_min=" + std::to_string(motor.speedbrake.ratioUpMin) +
                ", ratio_down_max=" + std::to_string(motor.speedbrake.ratioDownMax) +
                ", hold_ms=" + std::to_string(motor.speedbrake.holdMs));
        logLine("FSC: profile motor trim_indicator: wheel_dataref=" + motor.trimIndicator.wheelDatarefPath +
                ", wheel_min_ref=" + motor.trimIndicator.wheelMinRef +
                ", wheel_max_ref=" + motor.trimIndicator.wheelMaxRef +
                ", arrow_min_ref=" + motor.trimIndicator.arrowMinRef +
                ", arrow_max_ref=" + motor.trimIndicator.arrowMaxRef +
                ", hold_ms=" + std::to_string(motor.trimIndicator.holdMs));
        std::string onGround = motor.onGroundDatarefPath.empty()
                                   ? "default(sim/flightmodel/failures/onground_any)"
                                   : motor.onGroundDatarefPath;
        std::string leftToe = motor.leftToeBrakeDatarefPath.empty()
                                  ? "default(sim/cockpit2/controls/left_brake_ratio)"
                                  : motor.leftToeBrakeDatarefPath;
        std::string rightToe = motor.rightToeBrakeDatarefPath.empty()
                                   ? "default(sim/cockpit2/controls/right_brake_ratio)"
                                   : motor.rightToeBrakeDatarefPath;
        logLine("FSC: profile motor aux: on_ground_dataref=" + onGround +
                ", left_toe_brake_dataref=" + leftToe +
                ", right_toe_brake_dataref=" + rightToe);
    }
}

static void logFscProfileLoaded(const FscProfileRecord& record, bool verbose) {
    const auto& profile = record.runtime;
    size_t axes = 0;
    size_t switches = 0;
    size_t indicators = 0;
    for (const auto& axis : profile.axes) {
        if (axis.defined) ++axes;
    }
    for (const auto& sw : profile.switches) {
        if (sw.defined) ++switches;
    }
    for (const auto& indicator : profile.indicators) {
        if (indicator.defined) ++indicators;
    }
    std::string tailnums = joinStrings(profile.tailnums, ",");
    if (tailnums.empty()) {
        tailnums = "none";
    }
    logLine("FSC: profile loaded: id=" + profile.profileId +
            ", name=" + profile.name +
            ", version=" + std::to_string(profile.version) +
            ", tailnums=" + tailnums +
            ", axes=" + std::to_string(axes) +
            ", switches=" + std::to_string(switches) +
            ", indicators=" + std::to_string(indicators) +
            ", speedbrake=" + bool01(profile.speedbrake.enabled) +
            ", flaps=" + bool01(profile.flaps.enabled) +
            ", motorized=" + bool01(profile.motorized.enabled) +
            ", path=" + record.path);
    if (verbose) {
        logFscProfileDetails(profile);
    }
}

static void profileError(std::vector<std::string>& errors, const std::string& msg) {
    errors.push_back(msg);
}

static void checkAllowedKeys(const JsonValue& obj,
                             const std::unordered_set<std::string>& allowed,
                             const std::string& ctx,
                             std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": expected object");
        return;
    }
    for (const auto& kv : obj.objectValue) {
        if (allowed.find(kv.first) == allowed.end()) {
            profileError(errors, ctx + ": unknown key '" + kv.first + "'");
        }
    }
}

static const JsonValue* requireField(const JsonValue& obj,
                                     const std::string& key,
                                     const std::string& ctx,
                                     std::vector<std::string>& errors) {
    const JsonValue* v = jsonGet(obj, key);
    if (!v) {
        profileError(errors, ctx + ": missing key '" + key + "'");
    }
    return v;
}

static bool readStringField(const JsonValue& obj,
                            const std::string& key,
                            bool required,
                            std::string& out,
                            const std::string& ctx,
                            std::vector<std::string>& errors) {
    const JsonValue* v = jsonGet(obj, key);
    if (!v) {
        if (required) {
            profileError(errors, ctx + ": missing key '" + key + "'");
        }
        return false;
    }
    if (v->type != JsonValue::Type::String) {
        profileError(errors, ctx + ": key '" + key + "' must be string");
        return false;
    }
    out = v->stringValue;
    return true;
}

static bool readBoolField(const JsonValue& obj,
                          const std::string& key,
                          bool required,
                          bool& out,
                          const std::string& ctx,
                          std::vector<std::string>& errors) {
    const JsonValue* v = jsonGet(obj, key);
    if (!v) {
        if (required) {
            profileError(errors, ctx + ": missing key '" + key + "'");
        }
        return false;
    }
    if (v->type != JsonValue::Type::Bool) {
        profileError(errors, ctx + ": key '" + key + "' must be bool");
        return false;
    }
    out = v->boolValue;
    return true;
}

static bool readNumberField(const JsonValue& obj,
                            const std::string& key,
                            bool required,
                            double& out,
                            const std::string& ctx,
                            std::vector<std::string>& errors) {
    const JsonValue* v = jsonGet(obj, key);
    if (!v) {
        if (required) {
            profileError(errors, ctx + ": missing key '" + key + "'");
        }
        return false;
    }
    if (v->type != JsonValue::Type::Number) {
        profileError(errors, ctx + ": key '" + key + "' must be number");
        return false;
    }
    out = v->numberValue;
    return true;
}

static bool readNumberArray2(const JsonValue& obj,
                             const std::string& key,
                             bool required,
                             float& outMin,
                             float& outMax,
                             const std::string& ctx,
                             std::vector<std::string>& errors) {
    const JsonValue* v = jsonGet(obj, key);
    if (!v) {
        if (required) {
            profileError(errors, ctx + ": missing key '" + key + "'");
        }
        return false;
    }
    if (v->type != JsonValue::Type::Array || v->arrayValue.size() != 2) {
        profileError(errors, ctx + ": key '" + key + "' must be array of 2 numbers");
        return false;
    }
    if (v->arrayValue[0].type != JsonValue::Type::Number || v->arrayValue[1].type != JsonValue::Type::Number) {
        profileError(errors, ctx + ": key '" + key + "' must be array of numbers");
        return false;
    }
    outMin = static_cast<float>(v->arrayValue[0].numberValue);
    outMax = static_cast<float>(v->arrayValue[1].numberValue);
    return true;
}

static bool parseAction(const JsonValue& obj,
                        FscAction& out,
                        const std::string& ctx,
                        std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": action must be object");
        return false;
    }
    checkAllowedKeys(obj, {"type", "path", "phase", "index", "value"}, ctx, errors);
    std::string type;
    if (!readStringField(obj, "type", true, type, ctx, errors)) {
        return false;
    }
    std::string path;
    if (!readStringField(obj, "path", true, path, ctx, errors)) {
        return false;
    }
    out.path = path;
    if (type == "command") {
        out.type = FscAction::Type::Command;
        std::string phase;
        if (readStringField(obj, "phase", false, phase, ctx, errors)) {
            if (phase == "once") out.phase = FscAction::Phase::Once;
            else if (phase == "begin") out.phase = FscAction::Phase::Begin;
            else if (phase == "end") out.phase = FscAction::Phase::End;
            else profileError(errors, ctx + ": invalid phase '" + phase + "'");
        }
        if (jsonGet(obj, "value")) {
            profileError(errors, ctx + ": command action must not include 'value'");
        }
    } else if (type == "dataref") {
        out.type = FscAction::Type::Dataref;
        double value = 0.0;
        if (!readNumberField(obj, "value", true, value, ctx, errors)) {
            return false;
        }
        out.value = static_cast<float>(value);
        if (jsonGet(obj, "phase")) {
            profileError(errors, ctx + ": dataref action must not include 'phase'");
        }
        const JsonValue* idx = jsonGet(obj, "index");
        if (idx) {
            if (idx->type != JsonValue::Type::Number) {
                profileError(errors, ctx + ": index must be number");
            } else {
                out.index = static_cast<int>(idx->numberValue);
            }
        }
    } else {
        profileError(errors, ctx + ": invalid action type '" + type + "'");
        return false;
    }
    return true;
}

static bool parseActionsArray(const JsonValue& arr,
                              std::vector<FscAction>& out,
                              const std::string& ctx,
                              std::vector<std::string>& errors) {
    if (arr.type != JsonValue::Type::Array) {
        profileError(errors, ctx + ": actions must be array");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < arr.arrayValue.size(); ++i) {
        FscAction action;
        if (!parseAction(arr.arrayValue[i], action, ctx + "[" + std::to_string(i) + "]", errors)) {
            ok = false;
        } else {
            out.push_back(std::move(action));
        }
    }
    return ok;
}

static bool parseAxisMapping(const JsonValue& obj,
                             FscAxisMapping& out,
                             const std::string& ctx,
                             std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": axis mapping must be object");
        return false;
    }
    checkAllowedKeys(obj, {"source_ref_min", "source_ref_max", "invert", "invert_ref", "target_range", "targets"}, ctx, errors);
    readStringField(obj, "source_ref_min", true, out.sourceRefMin, ctx, errors);
    readStringField(obj, "source_ref_max", true, out.sourceRefMax, ctx, errors);
    readBoolField(obj, "invert", false, out.invert, ctx, errors);
    readStringField(obj, "invert_ref", false, out.invertRef, ctx, errors);
    if (!out.invertRef.empty() && jsonGet(obj, "invert")) {
        profileError(errors, ctx + ": use only one of invert or invert_ref");
    }
    readNumberArray2(obj, "target_range", true, out.targetMin, out.targetMax, ctx, errors);
    const JsonValue* targets = jsonGet(obj, "targets");
    if (!targets || targets->type != JsonValue::Type::Array) {
        profileError(errors, ctx + ": targets must be array");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < targets->arrayValue.size(); ++i) {
        const JsonValue& t = targets->arrayValue[i];
        std::string tctx = ctx + ".targets[" + std::to_string(i) + "]";
        if (t.type != JsonValue::Type::Object) {
            profileError(errors, tctx + ": target must be object");
            ok = false;
            continue;
        }
        checkAllowedKeys(t, {"type", "path", "index"}, tctx, errors);
        std::string type;
        if (!readStringField(t, "type", true, type, tctx, errors)) {
            ok = false;
            continue;
        }
        if (type != "dataref") {
            profileError(errors, tctx + ": target type must be 'dataref'");
            ok = false;
            continue;
        }
        FscAxisTarget target;
        if (!readStringField(t, "path", true, target.path, tctx, errors)) {
            ok = false;
            continue;
        }
        const JsonValue* idx = jsonGet(t, "index");
        if (idx) {
            if (idx->type != JsonValue::Type::Number) {
                profileError(errors, tctx + ": index must be number");
                ok = false;
            } else {
                target.index = static_cast<int>(idx->numberValue);
            }
        }
        out.targets.push_back(std::move(target));
    }
    out.defined = ok;
    return ok;
}

static bool parseSwitchMapping(const JsonValue& obj,
                               FscSwitchMapping& out,
                               const std::string& ctx,
                               std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": switch mapping must be object");
        return false;
    }
    checkAllowedKeys(obj, {"type", "positions", "on_press", "on_release", "on_cw", "on_ccw", "state_dataref", "state_on_min"}, ctx, errors);
    std::string type;
    if (!readStringField(obj, "type", true, type, ctx, errors)) {
        return false;
    }
    if (type == "latching") {
        out.type = FscSwitchMapping::Type::Latching;
    } else if (type == "latching_toggle") {
        out.type = FscSwitchMapping::Type::LatchingToggle;
    } else if (type == "momentary") {
        out.type = FscSwitchMapping::Type::Momentary;
    } else if (type == "encoder") {
        out.type = FscSwitchMapping::Type::Encoder;
    } else {
        profileError(errors, ctx + ": invalid switch type '" + type + "'");
        return false;
    }
    if (out.type == FscSwitchMapping::Type::Latching || out.type == FscSwitchMapping::Type::LatchingToggle) {
        const JsonValue* positions = jsonGet(obj, "positions");
        if (!positions || positions->type != JsonValue::Type::Object) {
            profileError(errors, ctx + ": positions must be object");
            return false;
        }
        checkAllowedKeys(*positions, {"on", "off"}, ctx + ".positions", errors);
        const JsonValue* on = jsonGet(*positions, "on");
        const JsonValue* off = jsonGet(*positions, "off");
        if (!on || !off) {
            profileError(errors, ctx + ": positions must include on/off");
            return false;
        }
        const JsonValue* onActions = jsonGet(*on, "actions");
        const JsonValue* offActions = jsonGet(*off, "actions");
        if (!onActions || !offActions) {
            profileError(errors, ctx + ": positions on/off must contain actions");
            return false;
        }
        parseActionsArray(*onActions, out.onActions, ctx + ".positions.on.actions", errors);
        parseActionsArray(*offActions, out.offActions, ctx + ".positions.off.actions", errors);
        if (out.type == FscSwitchMapping::Type::LatchingToggle) {
            readStringField(obj, "state_dataref", false, out.stateDatarefPath, ctx, errors);
            double v = 0.0;
            if (readNumberField(obj, "state_on_min", false, v, ctx, errors)) {
                out.stateOnMin = static_cast<float>(v);
            }
        }
    } else if (out.type == FscSwitchMapping::Type::Momentary) {
        const JsonValue* press = jsonGet(obj, "on_press");
        const JsonValue* release = jsonGet(obj, "on_release");
        if (!press || !release) {
            profileError(errors, ctx + ": momentary requires on_press and on_release");
            return false;
        }
        const JsonValue* pressActions = jsonGet(*press, "actions");
        const JsonValue* releaseActions = jsonGet(*release, "actions");
        if (!pressActions || !releaseActions) {
            profileError(errors, ctx + ": on_press/on_release must contain actions");
            return false;
        }
        parseActionsArray(*pressActions, out.pressActions, ctx + ".on_press.actions", errors);
        parseActionsArray(*releaseActions, out.releaseActions, ctx + ".on_release.actions", errors);
    } else if (out.type == FscSwitchMapping::Type::Encoder) {
        const JsonValue* cw = jsonGet(obj, "on_cw");
        const JsonValue* ccw = jsonGet(obj, "on_ccw");
        if (!cw || !ccw) {
            profileError(errors, ctx + ": encoder requires on_cw and on_ccw");
            return false;
        }
        const JsonValue* cwActions = jsonGet(*cw, "actions");
        const JsonValue* ccwActions = jsonGet(*ccw, "actions");
        if (!cwActions || !ccwActions) {
            profileError(errors, ctx + ": on_cw/on_ccw must contain actions");
            return false;
        }
        parseActionsArray(*cwActions, out.cwActions, ctx + ".on_cw.actions", errors);
        parseActionsArray(*ccwActions, out.ccwActions, ctx + ".on_ccw.actions", errors);
    }
    out.defined = true;
    return true;
}

static bool parseIndicatorMapping(const JsonValue& obj,
                                  FscIndicatorMapping& out,
                                  const std::string& ctx,
                                  std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": indicator must be object");
        return false;
    }
    checkAllowedKeys(obj, {"type", "source", "on_when", "invert"}, ctx, errors);
    std::string type;
    if (!readStringField(obj, "type", true, type, ctx, errors)) {
        return false;
    }
    if (type != "led") {
        profileError(errors, ctx + ": indicator type must be 'led'");
        return false;
    }
    const JsonValue* source = jsonGet(obj, "source");
    if (!source || source->type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": source must be object");
        return false;
    }
    checkAllowedKeys(*source, {"dataref"}, ctx + ".source", errors);
    if (!readStringField(*source, "dataref", true, out.datarefPath, ctx + ".source", errors)) {
        return false;
    }
    const JsonValue* onWhen = jsonGet(obj, "on_when");
    if (!onWhen || onWhen->type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": on_when must be object");
        return false;
    }
    checkAllowedKeys(*onWhen, {"min"}, ctx + ".on_when", errors);
    double v = 0.0;
    if (!readNumberField(*onWhen, "min", true, v, ctx + ".on_when", errors)) {
        return false;
    }
    out.onMin = static_cast<float>(v);
    readBoolField(obj, "invert", false, out.invert, ctx, errors);
    out.defined = true;
    return true;
}

static bool parseSpeedbrakeBehavior(const JsonValue& obj,
                                    FscSpeedbrakeBehavior& out,
                                    const std::string& ctx,
                                    std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": speedbrake must be object");
        return false;
    }
    checkAllowedKeys(obj, {"source_axis", "invert_ref", "ratio_dataref", "detents", "analog"}, ctx, errors);
    std::string sourceAxis;
    if (!readStringField(obj, "source_axis", true, sourceAxis, ctx, errors)) {
        return false;
    }
    if (sourceAxis != "speedbrake") {
        profileError(errors, ctx + ": source_axis must be 'speedbrake'");
        return false;
    }
    readStringField(obj, "invert_ref", false, out.invertRef, ctx, errors);
    readStringField(obj, "ratio_dataref", false, out.ratioDatarefPath, ctx, errors);

    const JsonValue* detents = jsonGet(obj, "detents");
    if (!detents || detents->type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": detents must be object");
        return false;
    }
    checkAllowedKeys(*detents, {"down", "armed", "up"}, ctx + ".detents", errors);
    auto parseDetent = [&](const char* name, FscSpeedbrakeDetent& detent) -> bool {
        const JsonValue* d = jsonGet(*detents, name);
        if (!d || d->type != JsonValue::Type::Object) {
            profileError(errors, ctx + ".detents: missing '" + std::string(name) + "'");
            return false;
        }
        std::string dctx = ctx + ".detents." + name;
        checkAllowedKeys(*d, {"source_ref", "tolerance", "actions", "actions_if_ratio_zero",
                              "actions_if_ratio_nonzero", "confirm_value"}, dctx, errors);
        readStringField(*d, "source_ref", true, detent.sourceRef, dctx, errors);
        double tol = 2.0;
        if (readNumberField(*d, "tolerance", false, tol, dctx, errors)) {
            detent.tolerance = static_cast<int>(tol);
        }
        const JsonValue* actions = jsonGet(*d, "actions");
        const JsonValue* actionsZero = jsonGet(*d, "actions_if_ratio_zero");
        const JsonValue* actionsNonzero = jsonGet(*d, "actions_if_ratio_nonzero");
        if (actions && (actionsZero || actionsNonzero)) {
            profileError(errors, dctx + ": use either actions or actions_if_ratio_*");
            return false;
        }
        if (actions) {
            parseActionsArray(*actions, detent.actions, dctx + ".actions", errors);
        } else if (actionsZero || actionsNonzero) {
            if (out.ratioDatarefPath.empty()) {
                profileError(errors, dctx + ": actions_if_ratio_* requires ratio_dataref");
                return false;
            }
            if (!actionsZero || !actionsNonzero) {
                profileError(errors, dctx + ": both actions_if_ratio_zero and actions_if_ratio_nonzero are required");
                return false;
            }
            parseActionsArray(*actionsZero, detent.actionsIfRatioZero, dctx + ".actions_if_ratio_zero", errors);
            parseActionsArray(*actionsNonzero, detent.actionsIfRatioNonzero, dctx + ".actions_if_ratio_nonzero", errors);
            detent.hasConditional = true;
        } else {
            profileError(errors, dctx + ": missing actions");
            return false;
        }
        const JsonValue* confirm = jsonGet(*d, "confirm_value");
        if (confirm) {
            if (confirm->type != JsonValue::Type::Number) {
                profileError(errors, dctx + ": confirm_value must be number");
            } else {
                detent.hasConfirm = true;
                detent.confirmValue = static_cast<float>(confirm->numberValue);
            }
        }
        return true;
    };

    bool ok = true;
    ok &= parseDetent("down", out.detentDown);
    ok &= parseDetent("armed", out.detentArmed);
    ok &= parseDetent("up", out.detentUp);

    const JsonValue* analog = jsonGet(obj, "analog");
    if (!analog || analog->type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": analog must be object");
        return false;
    }
    checkAllowedKeys(*analog, {"source_ref_min", "source_ref_max", "targets"}, ctx + ".analog", errors);
    readStringField(*analog, "source_ref_min", true, out.sourceRefMin, ctx + ".analog", errors);
    readStringField(*analog, "source_ref_max", true, out.sourceRefMax, ctx + ".analog", errors);
    const JsonValue* targets = jsonGet(*analog, "targets");
    if (!targets || targets->type != JsonValue::Type::Array) {
        profileError(errors, ctx + ".analog: targets must be array");
        return false;
    }
    for (size_t i = 0; i < targets->arrayValue.size(); ++i) {
        const JsonValue& t = targets->arrayValue[i];
        std::string tctx = ctx + ".analog.targets[" + std::to_string(i) + "]";
        if (t.type != JsonValue::Type::Object) {
            profileError(errors, tctx + ": target must be object");
            ok = false;
            continue;
        }
        checkAllowedKeys(t, {"type", "path", "target_range", "index"}, tctx, errors);
        std::string type;
        readStringField(t, "type", true, type, tctx, errors);
        if (type != "dataref") {
            profileError(errors, tctx + ": target type must be 'dataref'");
            ok = false;
            continue;
        }
        FscSpeedbrakeAnalogTarget target;
        readStringField(t, "path", true, target.path, tctx, errors);
        readNumberArray2(t, "target_range", true, target.targetMin, target.targetMax, tctx, errors);
        const JsonValue* idx = jsonGet(t, "index");
        if (idx) {
            if (idx->type != JsonValue::Type::Number) {
                profileError(errors, tctx + ": index must be number");
            } else {
                target.index = static_cast<int>(idx->numberValue);
            }
        }
        out.analogTargets.push_back(std::move(target));
    }
    out.enabled = ok;
    return ok;
}

static bool parseFlapsBehavior(const JsonValue& obj,
                               FscFlapsBehavior& out,
                               const std::string& ctx,
                               std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": flaps must be object");
        return false;
    }
    checkAllowedKeys(obj, {"source_axis", "mode", "positions"}, ctx, errors);
    std::string sourceAxis;
    if (!readStringField(obj, "source_axis", true, sourceAxis, ctx, errors)) {
        return false;
    }
    if (sourceAxis != "flaps") {
        profileError(errors, ctx + ": source_axis must be 'flaps'");
        return false;
    }
    std::string mode;
    if (!readStringField(obj, "mode", true, mode, ctx, errors)) {
        return false;
    }
    if (mode == "nearest") {
        out.modeNearest = true;
    } else if (mode == "exact") {
        out.modeNearest = false;
    } else {
        profileError(errors, ctx + ": mode must be 'nearest' or 'exact'");
        return false;
    }
    const JsonValue* positions = jsonGet(obj, "positions");
    if (!positions || positions->type != JsonValue::Type::Array) {
        profileError(errors, ctx + ": positions must be array");
        return false;
    }
    for (size_t i = 0; i < positions->arrayValue.size(); ++i) {
        const JsonValue& p = positions->arrayValue[i];
        std::string pctx = ctx + ".positions[" + std::to_string(i) + "]";
        if (p.type != JsonValue::Type::Object) {
            profileError(errors, pctx + ": position must be object");
            continue;
        }
        checkAllowedKeys(p, {"name", "source_ref", "tolerance", "actions"}, pctx, errors);
        FscFlapsPosition pos;
        readStringField(p, "name", true, pos.name, pctx, errors);
        readStringField(p, "source_ref", true, pos.sourceRef, pctx, errors);
        double tol = 2.0;
        if (readNumberField(p, "tolerance", false, tol, pctx, errors)) {
            pos.tolerance = static_cast<int>(tol);
        }
        const JsonValue* actions = jsonGet(p, "actions");
        if (!actions) {
            profileError(errors, pctx + ": missing actions");
        } else {
            parseActionsArray(*actions, pos.actions, pctx + ".actions", errors);
        }
        out.positions.push_back(std::move(pos));
    }
    out.enabled = true;
    return true;
}

static bool parseMotorizedBehavior(const JsonValue& obj,
                                   FscMotorizedBehavior& out,
                                   const std::string& ctx,
                                   std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": motorized must be object");
        return false;
    }
    checkAllowedKeys(obj, {"enabled", "throttle_follow", "speedbrake_motor", "trim_indicator"}, ctx, errors);
    readBoolField(obj, "enabled", true, out.enabled, ctx, errors);
    const JsonValue* throttle = jsonGet(obj, "throttle_follow");
    const JsonValue* speedbrake = jsonGet(obj, "speedbrake_motor");
    const JsonValue* trim = jsonGet(obj, "trim_indicator");
    if (!throttle || !speedbrake || !trim) {
        profileError(errors, ctx + ": motorized requires throttle_follow, speedbrake_motor, trim_indicator");
        return false;
    }
    if (throttle->type != JsonValue::Type::Object ||
        speedbrake->type != JsonValue::Type::Object ||
        trim->type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": motorized blocks must be objects");
        return false;
    }
    checkAllowedKeys(*throttle, {"lock_dataref", "arm_dataref", "lever_dataref", "motor_throttle1_min_ref",
                                 "motor_throttle1_max_ref", "motor_throttle2_min_ref", "motor_throttle2_max_ref",
                                 "update_rate_ref"}, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "lock_dataref", true, out.throttleFollow.lockDatarefPath, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "arm_dataref", true, out.throttleFollow.armDatarefPath, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "lever_dataref", true, out.throttleFollow.leverDatarefPath, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "motor_throttle1_min_ref", true, out.throttleFollow.thr1MinRef, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "motor_throttle1_max_ref", true, out.throttleFollow.thr1MaxRef, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "motor_throttle2_min_ref", true, out.throttleFollow.thr2MinRef, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "motor_throttle2_max_ref", true, out.throttleFollow.thr2MaxRef, ctx + ".throttle_follow", errors);
    readStringField(*throttle, "update_rate_ref", true, out.throttleFollow.updateRateRef, ctx + ".throttle_follow", errors);

    checkAllowedKeys(*speedbrake, {"ratio_dataref", "arm_ref", "up_ref", "tolerance", "ratio_up_min",
                                   "ratio_down_max", "motor_down_ref", "motor_up_ref", "hold_ms"}, ctx + ".speedbrake_motor", errors);
    readStringField(*speedbrake, "ratio_dataref", true, out.speedbrake.ratioDatarefPath, ctx + ".speedbrake_motor", errors);
    readStringField(*speedbrake, "arm_ref", true, out.speedbrake.armRef, ctx + ".speedbrake_motor", errors);
    readStringField(*speedbrake, "up_ref", true, out.speedbrake.upRef, ctx + ".speedbrake_motor", errors);
    readStringField(*speedbrake, "motor_down_ref", true, out.speedbrake.motorDownRef, ctx + ".speedbrake_motor", errors);
    readStringField(*speedbrake, "motor_up_ref", true, out.speedbrake.motorUpRef, ctx + ".speedbrake_motor", errors);
    double d = 0.0;
    if (readNumberField(*speedbrake, "tolerance", false, d, ctx + ".speedbrake_motor", errors)) {
        out.speedbrake.tolerance = static_cast<int>(d);
    }
    if (readNumberField(*speedbrake, "ratio_up_min", false, d, ctx + ".speedbrake_motor", errors)) {
        out.speedbrake.ratioUpMin = static_cast<float>(d);
    }
    if (readNumberField(*speedbrake, "ratio_down_max", false, d, ctx + ".speedbrake_motor", errors)) {
        out.speedbrake.ratioDownMax = static_cast<float>(d);
    }
    if (readNumberField(*speedbrake, "hold_ms", false, d, ctx + ".speedbrake_motor", errors)) {
        out.speedbrake.holdMs = static_cast<int>(d);
    }

    checkAllowedKeys(*trim, {"wheel_dataref", "wheel_min_ref", "wheel_max_ref", "arrow_min_ref", "arrow_max_ref", "hold_ms"},
                     ctx + ".trim_indicator", errors);
    readStringField(*trim, "wheel_dataref", true, out.trimIndicator.wheelDatarefPath, ctx + ".trim_indicator", errors);
    readStringField(*trim, "wheel_min_ref", true, out.trimIndicator.wheelMinRef, ctx + ".trim_indicator", errors);
    readStringField(*trim, "wheel_max_ref", true, out.trimIndicator.wheelMaxRef, ctx + ".trim_indicator", errors);
    readStringField(*trim, "arrow_min_ref", true, out.trimIndicator.arrowMinRef, ctx + ".trim_indicator", errors);
    readStringField(*trim, "arrow_max_ref", true, out.trimIndicator.arrowMaxRef, ctx + ".trim_indicator", errors);
    if (readNumberField(*trim, "hold_ms", false, d, ctx + ".trim_indicator", errors)) {
        out.trimIndicator.holdMs = static_cast<int>(d);
    }
    return true;
}

static void parseSyncSettings(const JsonValue& obj, FscSyncSettings& out, const std::string& ctx, std::vector<std::string>& errors) {
    if (obj.type != JsonValue::Type::Object) {
        profileError(errors, ctx + ": sync must be object");
        return;
    }
    checkAllowedKeys(obj, {"defer_until_datarefs", "startup_delay_sec", "resync_on_aircraft_loaded", "resync_interval_sec"}, ctx, errors);
    readBoolField(obj, "defer_until_datarefs", false, out.deferUntilDatarefs, ctx, errors);
    double v = 0.0;
    if (readNumberField(obj, "startup_delay_sec", false, v, ctx, errors)) {
        out.startupDelaySec = static_cast<int>(v);
    }
    readBoolField(obj, "resync_on_aircraft_loaded", false, out.resyncOnAircraftLoaded, ctx, errors);
    if (readNumberField(obj, "resync_interval_sec", false, v, ctx, errors)) {
        out.resyncIntervalSec = static_cast<float>(v);
    }
}

static bool parseFscProfile(const JsonValue& root,
                            FscProfileRuntime& out,
                            std::vector<std::string>& errors) {
    if (root.type != JsonValue::Type::Object) {
        profileError(errors, "profile: root must be object");
        return false;
    }
    checkAllowedKeys(root,
                     {"profile_id", "name", "version", "aircraft_match", "axes", "switches",
                      "indicators", "behaviors", "sync", "notes"},
                     "profile", errors);

    readStringField(root, "profile_id", true, out.profileId, "profile", errors);
    readStringField(root, "name", true, out.name, "profile", errors);
    const JsonValue* version = jsonGet(root, "version");
    if (!version || version->type != JsonValue::Type::Number) {
        profileError(errors, "profile: version must be number");
    } else {
        out.version = version->numberValue;
    }

    const JsonValue* match = requireField(root, "aircraft_match", "profile", errors);
    if (match && match->type == JsonValue::Type::Object) {
        checkAllowedKeys(*match, {"tailnums"}, "profile.aircraft_match", errors);
        const JsonValue* tailnums = jsonGet(*match, "tailnums");
        if (!tailnums || tailnums->type != JsonValue::Type::Array || tailnums->arrayValue.empty()) {
            profileError(errors, "profile.aircraft_match.tailnums must be non-empty array");
        } else {
            for (const auto& v : tailnums->arrayValue) {
                if (v.type != JsonValue::Type::String) {
                    profileError(errors, "profile.aircraft_match.tailnums entries must be strings");
                } else {
                    out.tailnums.push_back(v.stringValue);
                }
            }
        }
    } else if (match) {
        profileError(errors, "profile.aircraft_match must be object");
    }

    const JsonValue* axes = jsonGet(root, "axes");
    if (axes) {
        if (axes->type != JsonValue::Type::Object) {
            profileError(errors, "profile.axes must be object");
        } else {
            for (const auto& kv : axes->objectValue) {
                if (!kv.second) {
                    profileError(errors, "profile.axes." + kv.first + ": entry is null");
                    continue;
                }
                auto axisId = axisIdFromString(kv.first);
                if (!axisId.has_value()) {
                    profileError(errors, "profile.axes: unsupported axis '" + kv.first + "'");
                    continue;
                }
                FscAxisMapping mapping;
                parseAxisMapping(*kv.second, mapping, "profile.axes." + kv.first, errors);
                out.axes[static_cast<size_t>(*axisId)] = std::move(mapping);
            }
        }
    }

    const JsonValue* switches = jsonGet(root, "switches");
    if (switches) {
        if (switches->type != JsonValue::Type::Object) {
            profileError(errors, "profile.switches must be object");
        } else {
            for (const auto& kv : switches->objectValue) {
                if (!kv.second) {
                    profileError(errors, "profile.switches." + kv.first + ": entry is null");
                    continue;
                }
                auto switchId = switchIdFromString(kv.first);
                if (!switchId.has_value()) {
                    profileError(errors, "profile.switches: unsupported switch '" + kv.first + "'");
                    continue;
                }
                FscSwitchMapping mapping;
                parseSwitchMapping(*kv.second, mapping, "profile.switches." + kv.first, errors);
                out.switches[static_cast<size_t>(*switchId)] = std::move(mapping);
            }
        }
    }

    const JsonValue* indicators = jsonGet(root, "indicators");
    if (indicators) {
        if (indicators->type != JsonValue::Type::Object) {
            profileError(errors, "profile.indicators must be object");
        } else {
            for (const auto& kv : indicators->objectValue) {
                if (!kv.second) {
                    profileError(errors, "profile.indicators." + kv.first + ": entry is null");
                    continue;
                }
                auto indicatorId = indicatorIdFromString(kv.first);
                if (!indicatorId.has_value()) {
                    profileError(errors, "profile.indicators: unsupported indicator '" + kv.first + "'");
                    continue;
                }
                FscIndicatorMapping mapping;
                parseIndicatorMapping(*kv.second, mapping, "profile.indicators." + kv.first, errors);
                out.indicators[static_cast<size_t>(*indicatorId)] = std::move(mapping);
            }
        }
    }

    const JsonValue* behaviors = jsonGet(root, "behaviors");
    if (behaviors) {
        if (behaviors->type != JsonValue::Type::Object) {
            profileError(errors, "profile.behaviors must be object");
        } else {
            for (const auto& kv : behaviors->objectValue) {
                if (!kv.second) {
                    profileError(errors, "profile.behaviors." + kv.first + ": entry is null");
                    continue;
                }
                if (kv.first == "speedbrake") {
                    parseSpeedbrakeBehavior(*kv.second, out.speedbrake, "profile.behaviors.speedbrake", errors);
                } else if (kv.first == "flaps") {
                    parseFlapsBehavior(*kv.second, out.flaps, "profile.behaviors.flaps", errors);
                } else if (kv.first == "motorized") {
                    parseMotorizedBehavior(*kv.second, out.motorized, "profile.behaviors.motorized", errors);
                } else {
                    profileError(errors, "profile.behaviors: unsupported behavior '" + kv.first + "'");
                }
            }
        }
    }

    const JsonValue* sync = jsonGet(root, "sync");
    if (sync) {
        parseSyncSettings(*sync, out.sync, "profile.sync", errors);
    }

    return errors.empty();
}

static std::string fscProfilesDir() {
    return makePluginPath("Resources/plugins/" + std::string(PLUGIN_DIR) + "/profiles");
}

static bool readFileText(const std::string& path, std::string& out, std::string& err) {
    std::ifstream in(path, std::ios::in);
    if (!in.is_open()) {
        err = "failed to open file";
        return false;
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    if (!in.good() && !in.eof()) {
        err = "failed while reading file";
        return false;
    }
    out = oss.str();
    return true;
}

static bool loadFscProfiles() {
    g_fscProfiles.clear();
    g_fscProfileValid.store(false);
    g_fscProfileActive.store(false);
    g_fscProfileId.clear();
    g_fscProfilePath.clear();

    const std::string dir = fscProfilesDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        logLine("FSC: profiles directory not found: " + dir);
        return false;
    }
    if (!std::filesystem::is_directory(dir, ec)) {
        logLine("FSC: profiles path is not a directory: " + dir);
        return false;
    }

    bool hasErrors = false;
    std::unordered_set<std::string> profileIds;
    std::unordered_map<std::string, std::string> tailnumOwner;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            logLine("FSC: failed to read profiles directory: " + dir);
            hasErrors = true;
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto path = entry.path();
        if (path.extension() != ".json") {
            continue;
        }

        std::string text;
        std::string err;
        if (!readFileText(path.string(), text, err)) {
            logLine("FSC: failed to read profile " + path.string() + ": " + err);
            hasErrors = true;
            continue;
        }

        JsonValue root;
        std::string parseErr;
        if (!parseJson(text, root, parseErr)) {
            logLine("FSC: invalid JSON in " + path.string() + ": " + parseErr);
            hasErrors = true;
            continue;
        }

        FscProfileRuntime runtime;
        std::vector<std::string> errors;
        if (!parseFscProfile(root, runtime, errors)) {
            for (const auto& e : errors) {
                logLine("FSC: profile error in " + path.string() + ": " + e);
            }
            hasErrors = true;
            continue;
        }

        if (runtime.profileId.empty()) {
            logLine("FSC: profile in " + path.string() + " missing profile_id");
            hasErrors = true;
            continue;
        }
        if (!profileIds.insert(runtime.profileId).second) {
            logLine("FSC: duplicate profile_id '" + runtime.profileId + "' in " + path.string());
            hasErrors = true;
        }
        for (const auto& tail : runtime.tailnums) {
            auto it = tailnumOwner.find(tail);
            if (it != tailnumOwner.end()) {
                logLine("FSC: duplicate tailnum '" + tail + "' in " + path.string() + " (already in " + it->second + ")");
                hasErrors = true;
            } else {
                tailnumOwner.emplace(tail, path.string());
            }
        }

        FscProfileRecord record;
        record.path = path.string();
        record.runtime = std::move(runtime);
        g_fscProfiles.push_back(std::move(record));
        logFscProfileLoaded(g_fscProfiles.back(), g_prefs.fsc.debug);
    }

    if (g_fscProfiles.empty()) {
        logLine("FSC: no profiles found in " + dir);
    }

    g_fscProfileValid.store(!hasErrors);
    if (hasErrors) {
        logLine("FSC: profile load failed; FSC disabled until profiles are fixed.");
    }
    return !hasErrors;
}

static Prefs::FscCalib defaultFscCalibForType(Prefs::FscType type) {
    Prefs::FscCalib calib;
    // SEMIPRO defaults (Documentation/B738X.FSC_throttle_semi_pro.lua)
    if (type == Prefs::FscType::SemiPro) {
        calib.spoilersDown = 19;
        calib.spoilersArmed = 75;
        calib.spoilersMin = 100;
        calib.spoilersDetent = 185;
        calib.spoilersUp = 209;
        calib.throttle1Min = 43;
        calib.throttle1Full = 211;
        calib.throttle2Min = 43;
        calib.throttle2Full = 211;
        calib.reverser1Min = 12;
        calib.reverser1Max = 95;
        calib.reverser2Min = 12;
        calib.reverser2Max = 95;
        calib.flaps00 = 3;
        calib.flaps01 = 31;
        calib.flaps02 = 57;
        calib.flaps05 = 81;
        calib.flaps10 = 101;
        calib.flaps15 = 123;
        calib.flaps25 = 151;
        calib.flaps30 = 183;
        calib.flaps40 = 225;
        return calib;
    }
    // PRO defaults (Documentation/B738X.FSC_throttle_pro.lua)
    if (type == Prefs::FscType::Pro) {
        calib.spoilersDown = 19;
        calib.spoilersArmed = 75;
        calib.spoilersMin = 100;
        calib.spoilersDetent = 185;
        calib.spoilersUp = 209;
        calib.throttle1Min = 43;
        calib.throttle1Full = 213;
        calib.throttle2Min = 43;
        calib.throttle2Full = 213;
        calib.reverser1Min = 12;
        calib.reverser1Max = 95;
        calib.reverser2Min = 12;
        calib.reverser2Max = 95;
        // Not used by PRO in this plugin (flaps are reported as discrete detents),
        // but keep consistent values.
        calib.flaps00 = 0;
        calib.flaps01 = 1;
        calib.flaps02 = 2;
        calib.flaps05 = 5;
        calib.flaps10 = 10;
        calib.flaps15 = 15;
        calib.flaps25 = 25;
        calib.flaps30 = 30;
        calib.flaps40 = 40;
        return calib;
    }
    // MOTORIZED defaults (Documentation/B738X.FSC_throttle_motorized.lua)
    if (type == Prefs::FscType::Motorized) {
        calib.spoilersDown = 1;
        calib.spoilersArmed = 37;
        calib.spoilersMin = 49;
        calib.spoilersDetent = 156;
        calib.spoilersUp = 207;
        calib.throttle1Min = 50;
        calib.throttle1Full = 218;
        calib.throttle2Min = 50;
        calib.throttle2Full = 220;
        calib.reverser1Min = 5;
        calib.reverser1Max = 112;
        calib.reverser2Min = 5;
        calib.reverser2Max = 109;
        // Not used by MOTORIZED in this plugin (flaps are reported as discrete detents),
        // but keep consistent values.
        calib.flaps00 = 0;
        calib.flaps01 = 1;
        calib.flaps02 = 2;
        calib.flaps05 = 5;
        calib.flaps10 = 10;
        calib.flaps15 = 15;
        calib.flaps25 = 25;
        calib.flaps30 = 30;
        calib.flaps40 = 40;
        return calib;
    }
    return calib;
}

static void normalizeFscSerial(Prefs::FscSerial& serial) {
    if (serial.baud <= 0) {
        serial.baud = 115200;
    }
    if (serial.dataBits < 5 || serial.dataBits > 8) {
        serial.dataBits = 8;
    }
    if (serial.stopBits != 1 && serial.stopBits != 2) {
        serial.stopBits = 1;
    }
}

static void normalizeFscThrottleFilter(Prefs::FscPrefs& fsc) {
    if (fsc.throttleSmoothMs < 0) {
        fsc.throttleSmoothMs = 0;
    }
    if (fsc.throttleDeadband < 0) {
        fsc.throttleDeadband = 0;
    }
    if (fsc.throttleSyncBand < 0.0f) {
        fsc.throttleSyncBand = 0.0f;
    } else if (fsc.throttleSyncBand > 1.0f) {
        fsc.throttleSyncBand = 1.0f;
    }
}

static bool writeDefaultPrefsFile(const Prefs& prefs);

Prefs loadPrefs() {
    Prefs prefs;
    // defaults
    prefs.logfileEnabled = true;
    prefs.logfileName = PLUGIN_LOG_NAME;
    prefs.fsc.enabled = false;
    prefs.fsc.port = "/dev/tty.usbserial";
    prefs.fsc.type = Prefs::FscType::SemiPro;
    prefs.fsc.speedBrakeReversed = false;
    prefs.fsc.throttleSmoothMs = 60;
    prefs.fsc.throttleDeadband = 1;
    prefs.fsc.throttleSyncBand = 0.015f;
    prefs.fsc.debug = false;
    prefs.fsc.serial.baud = 115200;
    prefs.fsc.serial.dataBits = 8;
    prefs.fsc.serial.stopBits = 1;
    prefs.fsc.serial.parity = Prefs::FscParity::None;
    prefs.fsc.serial.dtr = true;
    prefs.fsc.serial.rts = true;
    prefs.fsc.serial.xonxoff = false;
    prefs.fsc.calib = defaultFscCalibForType(prefs.fsc.type);

    std::ifstream in(getPrefsPath());
    if (!in.is_open()) {
        writeDefaultPrefsFile(prefs);
        return prefs;
    }

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') {
            val.pop_back();
        }
        kv[key] = val;
    }

    if (auto it = kv.find("fsc.type"); it != kv.end()) {
        parseFscType(it->second, prefs.fsc.type);
    }
    prefs.fsc.calib = defaultFscCalibForType(prefs.fsc.type);

    for (const auto& it : kv) {
        const std::string& key = it.first;
        const std::string& val = it.second;
        if (key == "log.enabled") parseBool(val, prefs.logfileEnabled);
        else if (key == "log.file") prefs.logfileName = val;
        else if (key == "fsc.enabled") parseBool(val, prefs.fsc.enabled);
        else if (key == "fsc.port") prefs.fsc.port = val;
        else if (key == "fsc.type") parseFscType(val, prefs.fsc.type);
        else if (key == "fsc.speed_brake_reversed") parseBool(val, prefs.fsc.speedBrakeReversed);
        else if (key == "fsc.fuel_lever_inverted") parseBool(val, prefs.fsc.fuelLeverInverted);
        else if (key == "fsc.throttle_smooth_ms") prefs.fsc.throttleSmoothMs = std::stoi(val);
        else if (key == "fsc.throttle_deadband") prefs.fsc.throttleDeadband = std::stoi(val);
        else if (key == "fsc.throttle_sync_band") prefs.fsc.throttleSyncBand = std::stof(val);
        else if (key == "fsc.debug") parseBool(val, prefs.fsc.debug);
        else if (key == "fsc.baud") prefs.fsc.serial.baud = std::stoi(val);
        else if (key == "fsc.data_bits") prefs.fsc.serial.dataBits = std::stoi(val);
        else if (key == "fsc.parity") parseFscParity(val, prefs.fsc.serial.parity);
        else if (key == "fsc.stop_bits") prefs.fsc.serial.stopBits = std::stoi(val);
        else if (key == "fsc.dtr") parseBool(val, prefs.fsc.serial.dtr);
        else if (key == "fsc.rts") parseBool(val, prefs.fsc.serial.rts);
        else if (key == "fsc.xonxoff") parseBool(val, prefs.fsc.serial.xonxoff);
        else if (key == "fsc.calib.spoilers_down") prefs.fsc.calib.spoilersDown = std::stoi(val);
        else if (key == "fsc.calib.spoilers_armed") prefs.fsc.calib.spoilersArmed = std::stoi(val);
        else if (key == "fsc.calib.spoilers_min") prefs.fsc.calib.spoilersMin = std::stoi(val);
        else if (key == "fsc.calib.spoilers_detent") prefs.fsc.calib.spoilersDetent = std::stoi(val);
        else if (key == "fsc.calib.spoilers_up") prefs.fsc.calib.spoilersUp = std::stoi(val);
        else if (key == "fsc.calib.throttle1_min") prefs.fsc.calib.throttle1Min = std::stoi(val);
        else if (key == "fsc.calib.throttle1_full") prefs.fsc.calib.throttle1Full = std::stoi(val);
        else if (key == "fsc.calib.throttle2_min") prefs.fsc.calib.throttle2Min = std::stoi(val);
        else if (key == "fsc.calib.throttle2_full") prefs.fsc.calib.throttle2Full = std::stoi(val);
        else if (key == "fsc.calib.reverser1_min") prefs.fsc.calib.reverser1Min = std::stoi(val);
        else if (key == "fsc.calib.reverser1_max") prefs.fsc.calib.reverser1Max = std::stoi(val);
        else if (key == "fsc.calib.reverser2_min") prefs.fsc.calib.reverser2Min = std::stoi(val);
        else if (key == "fsc.calib.reverser2_max") prefs.fsc.calib.reverser2Max = std::stoi(val);
        else if (key == "fsc.calib.flaps_00") prefs.fsc.calib.flaps00 = std::stoi(val);
        else if (key == "fsc.calib.flaps_01") prefs.fsc.calib.flaps01 = std::stoi(val);
        else if (key == "fsc.calib.flaps_02") prefs.fsc.calib.flaps02 = std::stoi(val);
        else if (key == "fsc.calib.flaps_05") prefs.fsc.calib.flaps05 = std::stoi(val);
        else if (key == "fsc.calib.flaps_10") prefs.fsc.calib.flaps10 = std::stoi(val);
        else if (key == "fsc.calib.flaps_15") prefs.fsc.calib.flaps15 = std::stoi(val);
        else if (key == "fsc.calib.flaps_25") prefs.fsc.calib.flaps25 = std::stoi(val);
        else if (key == "fsc.calib.flaps_30") prefs.fsc.calib.flaps30 = std::stoi(val);
        else if (key == "fsc.calib.flaps_40") prefs.fsc.calib.flaps40 = std::stoi(val);
        else if (key == "fsc.motor.spoilers_down") prefs.fsc.motor.spoilersDown = std::stoi(val);
        else if (key == "fsc.motor.spoilers_up") prefs.fsc.motor.spoilersUp = std::stoi(val);
        else if (key == "fsc.motor.trim_arrow_02") prefs.fsc.motor.trimArrow02 = std::stoi(val);
        else if (key == "fsc.motor.trim_arrow_17") prefs.fsc.motor.trimArrow17 = std::stoi(val);
        else if (key == "fsc.motor.throttle1_min") prefs.fsc.motor.throttle1Min = std::stoi(val);
        else if (key == "fsc.motor.throttle1_max") prefs.fsc.motor.throttle1Max = std::stoi(val);
        else if (key == "fsc.motor.throttle2_min") prefs.fsc.motor.throttle2Min = std::stoi(val);
        else if (key == "fsc.motor.throttle2_max") prefs.fsc.motor.throttle2Max = std::stoi(val);
        else if (key == "fsc.motor.trim_wheel_02") prefs.fsc.motor.trimWheel02 = std::stof(val);
        else if (key == "fsc.motor.trim_wheel_17") prefs.fsc.motor.trimWheel17 = std::stof(val);
        else if (key == "fsc.motor.throttle_update_rate_sec") prefs.fsc.motor.throttleUpdateRateSec = std::stof(val);
    }
    normalizeFscSerial(prefs.fsc.serial);
    normalizeFscThrottleFilter(prefs.fsc);
    return prefs;
}

void logLine(const std::string& msg) {
    std::string line = "[" + std::string(PLUGIN_LOG_PREFIX) + "] " + msg + "\n";
    std::lock_guard<std::mutex> lock(g_logMutex);
    XPLMDebugString(line.c_str());
    if (g_fileLog.is_open()) {
        g_fileLog << line;
        g_fileLog.flush();
    }
}

static void openLogFileFromPrefs() {
    if (g_fileLog.is_open()) {
        g_fileLog.close();
    }
    if (!g_prefs.logfileEnabled) {
        return;
    }
    std::string logPath = makePluginPath("Resources/plugins/" + std::string(PLUGIN_DIR) + "/log/" + g_prefs.logfileName);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(logPath).parent_path(), ec);
    g_fileLog.open(logPath, std::ios::app);
    if (!g_fileLog.is_open()) {
        logLine("Warning: could not open logfile " + logPath);
    } else {
        logLine("Logfile opened at " + logPath);
    }
}

std::string readTailnum() {
    static XPLMDataRef tailRef = XPLMFindDataRef("sim/aircraft/view/acf_tailnum");
    if (!tailRef) {
        return {};
    }
    char buf[256]{};
    int n = XPLMGetDatab(tailRef, buf, 0, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    std::string raw(buf, static_cast<size_t>(n));
    auto nul = raw.find('\0');
    if (nul != std::string::npos) {
        raw.resize(nul);
    }
    return trimString(raw);
}

std::string fscTypeToString(Prefs::FscType type) {
    switch (type) {
        case Prefs::FscType::SemiPro: return "SEMIPRO";
        case Prefs::FscType::Pro: return "PRO";
        case Prefs::FscType::Motorized: return "MOTORIZED";
    }
    return "UNKNOWN";
}

std::string fscParityToString(Prefs::FscParity parity) {
    switch (parity) {
        case Prefs::FscParity::None: return "none";
        case Prefs::FscParity::Even: return "even";
        case Prefs::FscParity::Odd: return "odd";
    }
    return "unknown";
}

std::string fscSerialSummary(const Prefs::FscSerial& serial) {
    return "baud=" + std::to_string(serial.baud) +
           ", data_bits=" + std::to_string(serial.dataBits) +
           ", parity=" + fscParityToString(serial.parity) +
           ", stop_bits=" + std::to_string(serial.stopBits) +
           ", dtr=" + std::string(serial.dtr ? "1" : "0") +
           ", rts=" + std::string(serial.rts ? "1" : "0") +
           ", xonxoff=" + std::string(serial.xonxoff ? "1" : "0");
}

static std::vector<std::string> buildDefaultPrefsLines(const Prefs& prefs) {
    std::vector<std::string> lines;
    lines.push_back("# " + std::string(PLUGIN_PREFS_FILE));
    lines.push_back("# Auto-generated by " + std::string(PLUGIN_NAME) + " on first run.");
    lines.push_back("# Edit this file and run " + std::string(PLUGIN_COMMAND_PREFIX) + "/reload_prefs.");
    lines.push_back("");
    lines.push_back("# Logging");
    lines.push_back("log.enabled=" + bool01(prefs.logfileEnabled));
    lines.push_back("log.file=" + prefs.logfileName);
    lines.push_back("");
    lines.push_back("# FSC Throttle Quadrant");
    lines.push_back("fsc.enabled=" + bool01(prefs.fsc.enabled));
    lines.push_back("fsc.type=" + fscTypeToString(prefs.fsc.type));
    lines.push_back("fsc.port=" + prefs.fsc.port);
    lines.push_back("fsc.speed_brake_reversed=" + bool01(prefs.fsc.speedBrakeReversed));
    lines.push_back("fsc.fuel_lever_inverted=" + bool01(prefs.fsc.fuelLeverInverted));
    lines.push_back("fsc.throttle_smooth_ms=" + std::to_string(prefs.fsc.throttleSmoothMs));
    lines.push_back("fsc.throttle_deadband=" + std::to_string(prefs.fsc.throttleDeadband));
    lines.push_back("fsc.throttle_sync_band=" + std::to_string(prefs.fsc.throttleSyncBand));
    lines.push_back("fsc.debug=" + bool01(prefs.fsc.debug));
    lines.push_back("fsc.baud=" + std::to_string(prefs.fsc.serial.baud));
    lines.push_back("fsc.data_bits=" + std::to_string(prefs.fsc.serial.dataBits));
    lines.push_back("fsc.parity=" + fscParityToString(prefs.fsc.serial.parity));
    lines.push_back("fsc.stop_bits=" + std::to_string(prefs.fsc.serial.stopBits));
    lines.push_back("fsc.dtr=" + bool01(prefs.fsc.serial.dtr));
    lines.push_back("fsc.rts=" + bool01(prefs.fsc.serial.rts));
    lines.push_back("fsc.xonxoff=" + bool01(prefs.fsc.serial.xonxoff));
    lines.push_back("");
    lines.push_back("# FSC calibration");
    const auto& c = prefs.fsc.calib;
    lines.push_back("fsc.calib.spoilers_down=" + std::to_string(c.spoilersDown));
    lines.push_back("fsc.calib.spoilers_armed=" + std::to_string(c.spoilersArmed));
    lines.push_back("fsc.calib.spoilers_min=" + std::to_string(c.spoilersMin));
    lines.push_back("fsc.calib.spoilers_detent=" + std::to_string(c.spoilersDetent));
    lines.push_back("fsc.calib.spoilers_up=" + std::to_string(c.spoilersUp));
    lines.push_back("fsc.calib.throttle1_min=" + std::to_string(c.throttle1Min));
    lines.push_back("fsc.calib.throttle1_full=" + std::to_string(c.throttle1Full));
    lines.push_back("fsc.calib.throttle2_min=" + std::to_string(c.throttle2Min));
    lines.push_back("fsc.calib.throttle2_full=" + std::to_string(c.throttle2Full));
    lines.push_back("fsc.calib.reverser1_min=" + std::to_string(c.reverser1Min));
    lines.push_back("fsc.calib.reverser1_max=" + std::to_string(c.reverser1Max));
    lines.push_back("fsc.calib.reverser2_min=" + std::to_string(c.reverser2Min));
    lines.push_back("fsc.calib.reverser2_max=" + std::to_string(c.reverser2Max));
    if (prefs.fsc.type == Prefs::FscType::SemiPro) {
        lines.push_back("fsc.calib.flaps_00=" + std::to_string(c.flaps00));
        lines.push_back("fsc.calib.flaps_01=" + std::to_string(c.flaps01));
        lines.push_back("fsc.calib.flaps_02=" + std::to_string(c.flaps02));
        lines.push_back("fsc.calib.flaps_05=" + std::to_string(c.flaps05));
        lines.push_back("fsc.calib.flaps_10=" + std::to_string(c.flaps10));
        lines.push_back("fsc.calib.flaps_15=" + std::to_string(c.flaps15));
        lines.push_back("fsc.calib.flaps_25=" + std::to_string(c.flaps25));
        lines.push_back("fsc.calib.flaps_30=" + std::to_string(c.flaps30));
        lines.push_back("fsc.calib.flaps_40=" + std::to_string(c.flaps40));
    }
    lines.push_back("");
    lines.push_back("# FSC motorized tuning");
    const auto& m = prefs.fsc.motor;
    lines.push_back("fsc.motor.spoilers_down=" + std::to_string(m.spoilersDown));
    lines.push_back("fsc.motor.spoilers_up=" + std::to_string(m.spoilersUp));
    lines.push_back("fsc.motor.throttle1_min=" + std::to_string(m.throttle1Min));
    lines.push_back("fsc.motor.throttle1_max=" + std::to_string(m.throttle1Max));
    lines.push_back("fsc.motor.throttle2_min=" + std::to_string(m.throttle2Min));
    lines.push_back("fsc.motor.throttle2_max=" + std::to_string(m.throttle2Max));
    lines.push_back("fsc.motor.trim_arrow_02=" + std::to_string(m.trimArrow02));
    lines.push_back("fsc.motor.trim_arrow_17=" + std::to_string(m.trimArrow17));
    lines.push_back("fsc.motor.trim_wheel_02=" + std::to_string(m.trimWheel02));
    lines.push_back("fsc.motor.trim_wheel_17=" + std::to_string(m.trimWheel17));
    lines.push_back("fsc.motor.throttle_update_rate_sec=" + std::to_string(m.throttleUpdateRateSec));
    return lines;
}

static bool writeDefaultPrefsFile(const Prefs& prefs) {
    const std::string path = getPrefsPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        logLine("Prefs: failed to create directory for " + path + " (" + ec.message() + ")");
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        logLine("Prefs: failed to create default prefs at " + path);
        return false;
    }
    const auto lines = buildDefaultPrefsLines(prefs);
    for (const auto& line : lines) {
        out << line << "\n";
    }
    logLine("Prefs: created default prefs at " + path);
    return true;
}

std::string hexByte(uint8_t v) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(v);
    return oss.str();
}

static bool bindCommandPath(const std::string& path,
                            XPLMCommandRef& out,
                            bool logMissing,
                            const std::string& ctx,
                            bool& missing) {
    if (path.empty()) {
        if (logMissing) {
            logLine("FSC: missing command path for " + ctx);
        }
        missing = true;
        return false;
    }
    out = XPLMFindCommand(path.c_str());
    if (!out) {
        if (logMissing) {
            logLine("FSC: command not found for " + ctx + ": " + path);
        }
        missing = true;
        return false;
    }
    return true;
}

static bool bindDatarefPath(const std::string& path,
                            XPLMDataRef& out,
                            int& outType,
                            bool logMissing,
                            const std::string& ctx,
                            bool& missing) {
    if (path.empty()) {
        if (logMissing) {
            logLine("FSC: missing dataref path for " + ctx);
        }
        missing = true;
        return false;
    }
    out = XPLMFindDataRef(path.c_str());
    if (!out) {
        if (logMissing) {
            logLine("FSC: dataref not found for " + ctx + ": " + path);
        }
        missing = true;
        return false;
    }
    outType = XPLMGetDataRefTypes(out);
    return true;
}

static bool requirePrefIntRef(const std::string& key,
                              const std::string& ctx,
                              bool logMissing,
                              bool& missing) {
    int v = 0;
    if (!getPrefIntByKey(key, v)) {
        if (logMissing) {
            logLine("FSC: unknown int pref key for " + ctx + ": " + key);
        }
        missing = true;
        return false;
    }
    return true;
}

static bool requirePrefFloatRef(const std::string& key,
                                const std::string& ctx,
                                bool logMissing,
                                bool& missing) {
    float v = 0.0f;
    if (!getPrefFloatByKey(key, v)) {
        if (logMissing) {
            logLine("FSC: unknown float pref key for " + ctx + ": " + key);
        }
        missing = true;
        return false;
    }
    return true;
}

static bool requirePrefBoolRef(const std::string& key,
                               const std::string& ctx,
                               bool logMissing,
                               bool& missing) {
    bool v = false;
    if (!getPrefBoolByKey(key, v)) {
        if (logMissing) {
            logLine("FSC: unknown bool pref key for " + ctx + ": " + key);
        }
        missing = true;
        return false;
    }
    return true;
}

static void bindActions(std::vector<FscAction>& actions,
                        bool logMissing,
                        bool& missing) {
    for (auto& action : actions) {
        if (action.type == FscAction::Type::Command) {
            bindCommandPath(action.path, action.cmd, logMissing, "action", missing);
        } else {
            bindDatarefPath(action.path, action.dataref, action.datarefType, logMissing, "action", missing);
            if (action.dataref &&
                (action.datarefType & (xplmType_FloatArray | xplmType_IntArray)) &&
                action.index < 0) {
                if (logMissing) {
                    logLine("FSC: dataref array index required for action: " + action.path);
                }
                missing = true;
            }
        }
    }
}

static bool resolveFscProfileBindings(bool logMissing, bool& missingRefs) {
    missingRefs = false;

    for (auto& mapping : g_fscProfileRuntime.axes) {
        if (!mapping.defined) {
            continue;
        }
        requirePrefIntRef(mapping.sourceRefMin, "axis source_ref_min", logMissing, missingRefs);
        requirePrefIntRef(mapping.sourceRefMax, "axis source_ref_max", logMissing, missingRefs);
        if (!mapping.invertRef.empty()) {
            requirePrefBoolRef(mapping.invertRef, "axis invert_ref", logMissing, missingRefs);
        }
        for (auto& target : mapping.targets) {
            bindDatarefPath(target.path, target.dataref, target.datarefType, logMissing, "axis target", missingRefs);
            if (target.dataref &&
                (target.datarefType & (xplmType_FloatArray | xplmType_IntArray)) &&
                target.index < 0) {
                if (logMissing) {
                    logLine("FSC: dataref array index required for axis target: " + target.path);
                }
                missingRefs = true;
            }
        }
    }

    for (auto& mapping : g_fscProfileRuntime.switches) {
        if (!mapping.defined) {
            continue;
        }
        if (!mapping.stateDatarefPath.empty()) {
            bindDatarefPath(mapping.stateDatarefPath, mapping.stateDataref, mapping.stateDatarefType, logMissing, "switch state_dataref", missingRefs);
        }
        bindActions(mapping.onActions, logMissing, missingRefs);
        bindActions(mapping.offActions, logMissing, missingRefs);
        bindActions(mapping.pressActions, logMissing, missingRefs);
        bindActions(mapping.releaseActions, logMissing, missingRefs);
        bindActions(mapping.cwActions, logMissing, missingRefs);
        bindActions(mapping.ccwActions, logMissing, missingRefs);
    }

    for (auto& mapping : g_fscProfileRuntime.indicators) {
        if (!mapping.defined) {
            continue;
        }
        bindDatarefPath(mapping.datarefPath, mapping.dataref, mapping.datarefType, logMissing, "indicator source", missingRefs);
    }

    if (g_fscProfileRuntime.speedbrake.enabled) {
        const auto& sb = g_fscProfileRuntime.speedbrake;
        if (!sb.invertRef.empty()) {
            requirePrefBoolRef(sb.invertRef, "speedbrake invert_ref", logMissing, missingRefs);
        }
        if (!sb.ratioDatarefPath.empty()) {
            bindDatarefPath(sb.ratioDatarefPath, g_fscProfileRuntime.speedbrake.ratioDataref,
                            g_fscProfileRuntime.speedbrake.ratioDatarefType, logMissing, "speedbrake ratio_dataref", missingRefs);
        }
        requirePrefIntRef(sb.detentDown.sourceRef, "speedbrake detent down source_ref", logMissing, missingRefs);
        requirePrefIntRef(sb.detentArmed.sourceRef, "speedbrake detent armed source_ref", logMissing, missingRefs);
        requirePrefIntRef(sb.detentUp.sourceRef, "speedbrake detent up source_ref", logMissing, missingRefs);
        requirePrefIntRef(sb.sourceRefMin, "speedbrake analog source_ref_min", logMissing, missingRefs);
        requirePrefIntRef(sb.sourceRefMax, "speedbrake analog source_ref_max", logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentDown.actions, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentDown.actionsIfRatioZero, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentDown.actionsIfRatioNonzero, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentArmed.actions, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentArmed.actionsIfRatioZero, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentArmed.actionsIfRatioNonzero, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentUp.actions, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentUp.actionsIfRatioZero, logMissing, missingRefs);
        bindActions(g_fscProfileRuntime.speedbrake.detentUp.actionsIfRatioNonzero, logMissing, missingRefs);
        for (auto& target : g_fscProfileRuntime.speedbrake.analogTargets) {
            bindDatarefPath(target.path, target.dataref, target.datarefType, logMissing, "speedbrake analog target", missingRefs);
            if (target.dataref &&
                (target.datarefType & (xplmType_FloatArray | xplmType_IntArray)) &&
                target.index < 0) {
                if (logMissing) {
                    logLine("FSC: dataref array index required for speedbrake target: " + target.path);
                }
                missingRefs = true;
            }
        }
    }

    if (g_fscProfileRuntime.flaps.enabled) {
        for (auto& pos : g_fscProfileRuntime.flaps.positions) {
            requirePrefIntRef(pos.sourceRef, "flaps position source_ref", logMissing, missingRefs);
            bindActions(pos.actions, logMissing, missingRefs);
        }
    }

    if (g_fscProfileRuntime.motorized.enabled && g_prefs.fsc.type == Prefs::FscType::Motorized) {
        auto& motor = g_fscProfileRuntime.motorized;
        bindDatarefPath(motor.throttleFollow.lockDatarefPath, motor.throttleFollow.lockDataref,
                        motor.throttleFollow.lockDatarefType, logMissing, "motor throttle lock_dataref", missingRefs);
        bindDatarefPath(motor.throttleFollow.armDatarefPath, motor.throttleFollow.armDataref,
                        motor.throttleFollow.armDatarefType, logMissing, "motor throttle arm_dataref", missingRefs);
        bindDatarefPath(motor.throttleFollow.leverDatarefPath, motor.throttleFollow.leverDataref,
                        motor.throttleFollow.leverDatarefType, logMissing, "motor throttle lever_dataref", missingRefs);
        requirePrefIntRef(motor.throttleFollow.thr1MinRef, "motor throttle1 min ref", logMissing, missingRefs);
        requirePrefIntRef(motor.throttleFollow.thr1MaxRef, "motor throttle1 max ref", logMissing, missingRefs);
        requirePrefIntRef(motor.throttleFollow.thr2MinRef, "motor throttle2 min ref", logMissing, missingRefs);
        requirePrefIntRef(motor.throttleFollow.thr2MaxRef, "motor throttle2 max ref", logMissing, missingRefs);
        requirePrefFloatRef(motor.throttleFollow.updateRateRef, "motor throttle update_rate_ref", logMissing, missingRefs);

        bindDatarefPath(motor.speedbrake.ratioDatarefPath, motor.speedbrake.ratioDataref,
                        motor.speedbrake.ratioDatarefType, logMissing, "motor speedbrake ratio_dataref", missingRefs);
        requirePrefIntRef(motor.speedbrake.armRef, "motor speedbrake arm_ref", logMissing, missingRefs);
        requirePrefIntRef(motor.speedbrake.upRef, "motor speedbrake up_ref", logMissing, missingRefs);
        requirePrefIntRef(motor.speedbrake.motorDownRef, "motor speedbrake motor_down_ref", logMissing, missingRefs);
        requirePrefIntRef(motor.speedbrake.motorUpRef, "motor speedbrake motor_up_ref", logMissing, missingRefs);

        bindDatarefPath(motor.trimIndicator.wheelDatarefPath, motor.trimIndicator.wheelDataref,
                        motor.trimIndicator.wheelDatarefType, logMissing, "motor trim wheel_dataref", missingRefs);
        requirePrefFloatRef(motor.trimIndicator.wheelMinRef, "motor trim wheel_min_ref", logMissing, missingRefs);
        requirePrefFloatRef(motor.trimIndicator.wheelMaxRef, "motor trim wheel_max_ref", logMissing, missingRefs);
        requirePrefIntRef(motor.trimIndicator.arrowMinRef, "motor trim arrow_min_ref", logMissing, missingRefs);
        requirePrefIntRef(motor.trimIndicator.arrowMaxRef, "motor trim arrow_max_ref", logMissing, missingRefs);

        if (motor.onGroundDatarefPath.empty()) {
            motor.onGroundDatarefPath = "sim/flightmodel/failures/onground_any";
        }
        if (motor.leftToeBrakeDatarefPath.empty()) {
            motor.leftToeBrakeDatarefPath = "sim/cockpit2/controls/left_brake_ratio";
        }
        if (motor.rightToeBrakeDatarefPath.empty()) {
            motor.rightToeBrakeDatarefPath = "sim/cockpit2/controls/right_brake_ratio";
        }
        bindDatarefPath(motor.onGroundDatarefPath, motor.onGroundDataref, motor.onGroundDatarefType,
                        logMissing, "motor on_ground_dataref", missingRefs);
        bindDatarefPath(motor.leftToeBrakeDatarefPath, motor.leftToeBrakeDataref, motor.leftToeBrakeDatarefType,
                        logMissing, "motor left_toe_brake_dataref", missingRefs);
        bindDatarefPath(motor.rightToeBrakeDatarefPath, motor.rightToeBrakeDataref, motor.rightToeBrakeDatarefType,
                        logMissing, "motor right_toe_brake_dataref", missingRefs);
    }

    return !missingRefs;
}

static void scheduleFscDeferredInit(std::chrono::seconds delay) {
    g_fscDeferredInit.pending = true;
    g_fscDeferredInit.due = std::chrono::steady_clock::now() + delay;
    g_fscDeferredInit.attempts = 0;
}

static bool refreshFscProfile(bool logMissing) {
    g_fscProfileActive.store(false);
    g_fscProfileId.clear();
    g_fscProfilePath.clear();
    g_fscProfileRuntime = FscProfileRuntime{};
    for (auto& s : g_fscSwitchState) {
        s.known = false;
        s.value = false;
    }
    g_fscResyncPending.store(false);
    g_fscAxisResyncPending.store(false);
    g_fscAxisResyncDue = std::chrono::steady_clock::time_point{};
    g_fscDeferredInit.pending = false;

    if (!g_fscProfileValid.load()) {
        return false;
    }

    const std::string tail = readTailnum();
    if (tail.empty()) {
        logLine("FSC: tailnum not available, profile selection deferred.");
        scheduleFscDeferredInit(std::chrono::seconds(5));
        return false;
    }

    const FscProfileRecord* match = nullptr;
    for (const auto& record : g_fscProfiles) {
        for (const auto& t : record.runtime.tailnums) {
            if (t == tail) {
                match = &record;
                break;
            }
        }
        if (match) {
            break;
        }
    }

    if (!match) {
        logLine("FSC: no profile matched tailnum '" + tail + "'");
        return false;
    }

    g_fscProfileRuntime = match->runtime;
    g_fscProfileId = g_fscProfileRuntime.profileId;
    g_fscProfilePath = match->path;

    bool missingRefs = false;
    bool ready = resolveFscProfileBindings(logMissing, missingRefs);
    if (!ready && g_fscProfileRuntime.sync.deferUntilDatarefs) {
        logLine("FSC: profile '" + g_fscProfileId + "' waiting for datarefs/commands; deferring init.");
        scheduleFscDeferredInit(std::chrono::seconds(g_fscProfileRuntime.sync.startupDelaySec));
        return false;
    }

    if (missingRefs && logMissing) {
        logLine("FSC: profile '" + g_fscProfileId + "' active with missing bindings.");
    }

    g_fscProfileActive.store(true);
    if (g_fscProfileRuntime.sync.resyncOnAircraftLoaded) {
        g_fscResyncPending.store(true);
        scheduleFscAxisResync();
    }
    g_fscLastResync = std::chrono::steady_clock::now();
    logLine("FSC: active profile '" + g_fscProfileRuntime.profileId + "' (" + g_fscProfileRuntime.name + ")");
    return true;
}

static XPWidgetID createCaption(int left, int top, int right, int bottom, const char* text, XPWidgetID container) {
    return XPCreateWidget(left, top, right, bottom, 1, text, 0, container, xpWidgetClass_Caption);
}

static XPWidgetID createTextField(int left, int top, int right, int bottom, const char* text, int maxChars, XPWidgetID container) {
    XPWidgetID widget = XPCreateWidget(left, top, right, bottom, 1, text, 0, container, xpWidgetClass_TextField);
    XPSetWidgetProperty(widget, xpProperty_TextFieldType, xpTextEntryField);
    if (maxChars > 0) {
        XPSetWidgetProperty(widget, xpProperty_MaxCharacters, maxChars);
    }
    return widget;
}

static XPWidgetID createCheckbox(int left, int top, int right, int bottom, const char* label, XPWidgetID container) {
    XPWidgetID widget = XPCreateWidget(left, top, right, bottom, 1, label, 0, container, xpWidgetClass_Button);
    XPSetWidgetProperty(widget, xpProperty_ButtonType, xpRadioButton);
    XPSetWidgetProperty(widget, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);
    return widget;
}

static XPWidgetID createRadioButton(int left, int top, int right, int bottom, const char* label, XPWidgetID container) {
    XPWidgetID widget = XPCreateWidget(left, top, right, bottom, 1, label, 0, container, xpWidgetClass_Button);
    XPSetWidgetProperty(widget, xpProperty_ButtonType, xpRadioButton);
    XPSetWidgetProperty(widget, xpProperty_ButtonBehavior, xpButtonBehaviorRadioButton);
    return widget;
}

static XPWidgetID createPushButton(int left, int top, int right, int bottom, const char* label, XPWidgetID container) {
    XPWidgetID widget = XPCreateWidget(left, top, right, bottom, 1, label, 0, container, xpWidgetClass_Button);
    XPSetWidgetProperty(widget, xpProperty_ButtonType, xpPushButton);
    XPSetWidgetProperty(widget, xpProperty_ButtonBehavior, xpButtonBehaviorPushButton);
    return widget;
}

static void setWidgetBool(XPWidgetID widget, bool value) {
    if (!widget) {
        return;
    }
    XPSetWidgetProperty(widget, xpProperty_ButtonState, value ? 1 : 0);
}

static bool getWidgetBool(XPWidgetID widget) {
    if (!widget) {
        return false;
    }
    return XPGetWidgetProperty(widget, xpProperty_ButtonState, nullptr) != 0;
}

static std::string getWidgetText(XPWidgetID widget) {
    if (!widget) {
        return {};
    }
    char buf[256]{};
    int len = XPGetWidgetDescriptor(widget, buf, static_cast<int>(sizeof(buf)));
    if (len <= 0) {
        return {};
    }
    if (len >= static_cast<int>(sizeof(buf))) {
        len = static_cast<int>(sizeof(buf)) - 1;
    }
    return std::string(buf, static_cast<size_t>(len));
}

static void setWidgetText(XPWidgetID widget, const std::string& text) {
    if (!widget) {
        return;
    }
    XPSetWidgetDescriptor(widget, text.c_str());
}

static void setFscCalibStatus(const std::string& msg) {
    if (msg.rfind("Calibration", 0) == 0) {
        g_fscCalibStatusText = msg;
    } else {
        g_fscCalibStatusText = "Calibration: " + msg;
    }
    if (g_fscCalibStatus) {
        XPSetWidgetDescriptor(g_fscCalibStatus, g_fscCalibStatusText.c_str());
    }
}

static void syncFscWindowFromPrefs() {
    if (!g_fscWindow) {
        return;
    }
    setWidgetBool(g_fscCheckEnabled, g_prefs.fsc.enabled);
    setWidgetBool(g_fscCheckFuelInvert, g_prefs.fsc.fuelLeverInverted);
    setWidgetBool(g_fscCheckSpeedbrakeRev, g_prefs.fsc.speedBrakeReversed);
    setWidgetBool(g_fscCheckDebug, g_prefs.fsc.debug);
    setWidgetBool(g_fscCheckDtr, g_prefs.fsc.serial.dtr);
    setWidgetBool(g_fscCheckRts, g_prefs.fsc.serial.rts);
    setWidgetBool(g_fscCheckXonxoff, g_prefs.fsc.serial.xonxoff);

    setWidgetBool(g_fscRadioTypeSemi, g_prefs.fsc.type == Prefs::FscType::SemiPro);
    setWidgetBool(g_fscRadioTypePro, g_prefs.fsc.type == Prefs::FscType::Pro);
    setWidgetBool(g_fscRadioTypeMotor, g_prefs.fsc.type == Prefs::FscType::Motorized);

    setWidgetBool(g_fscRadioParityNone, g_prefs.fsc.serial.parity == Prefs::FscParity::None);
    setWidgetBool(g_fscRadioParityEven, g_prefs.fsc.serial.parity == Prefs::FscParity::Even);
    setWidgetBool(g_fscRadioParityOdd, g_prefs.fsc.serial.parity == Prefs::FscParity::Odd);

    setWidgetText(g_fscFieldPort, g_prefs.fsc.port);
    setWidgetText(g_fscFieldBaud, std::to_string(g_prefs.fsc.serial.baud));
    setWidgetText(g_fscFieldDataBits, std::to_string(g_prefs.fsc.serial.dataBits));
    setWidgetText(g_fscFieldStopBits, std::to_string(g_prefs.fsc.serial.stopBits));
    setWidgetText(g_fscFieldThrottleSmoothMs, std::to_string(g_prefs.fsc.throttleSmoothMs));
    setWidgetText(g_fscFieldThrottleDeadband, std::to_string(g_prefs.fsc.throttleDeadband));
    setWidgetText(g_fscFieldThrottleSyncBand, std::to_string(g_prefs.fsc.throttleSyncBand));
    setWidgetText(g_fscFieldMotorSpoilersDown, std::to_string(g_prefs.fsc.motor.spoilersDown));
    setWidgetText(g_fscFieldMotorSpoilersUp, std::to_string(g_prefs.fsc.motor.spoilersUp));
    setWidgetText(g_fscFieldMotorThrottle1Min, std::to_string(g_prefs.fsc.motor.throttle1Min));
    setWidgetText(g_fscFieldMotorThrottle1Max, std::to_string(g_prefs.fsc.motor.throttle1Max));
    setWidgetText(g_fscFieldMotorThrottle2Min, std::to_string(g_prefs.fsc.motor.throttle2Min));
    setWidgetText(g_fscFieldMotorThrottle2Max, std::to_string(g_prefs.fsc.motor.throttle2Max));
    setWidgetText(g_fscFieldMotorTrimArrow02, std::to_string(g_prefs.fsc.motor.trimArrow02));
    setWidgetText(g_fscFieldMotorTrimArrow17, std::to_string(g_prefs.fsc.motor.trimArrow17));
    setWidgetText(g_fscFieldMotorTrimWheel02, std::to_string(g_prefs.fsc.motor.trimWheel02));
    setWidgetText(g_fscFieldMotorTrimWheel17, std::to_string(g_prefs.fsc.motor.trimWheel17));
    setWidgetText(g_fscFieldMotorThrottleRate, std::to_string(g_prefs.fsc.motor.throttleUpdateRateSec));

    if (g_fscCalibStatus) {
        XPSetWidgetDescriptor(g_fscCalibStatus, g_fscCalibStatusText.c_str());
    }
}

static bool parseIntField(const std::string& text, int& out) {
    std::string trimmed = trimString(text);
    if (trimmed.empty()) {
        return false;
    }
    try {
        size_t idx = 0;
        int value = std::stoi(trimmed, &idx);
        if (idx != trimmed.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseFloatField(const std::string& text, float& out) {
    std::string trimmed = trimString(text);
    if (trimmed.empty()) {
        return false;
    }
    try {
        size_t idx = 0;
        float value = std::stof(trimmed, &idx);
        if (idx != trimmed.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static void applyFscWindowSettings() {
    Prefs updated = g_prefs;
    updated.fsc.enabled = getWidgetBool(g_fscCheckEnabled);
    updated.fsc.fuelLeverInverted = getWidgetBool(g_fscCheckFuelInvert);
    updated.fsc.speedBrakeReversed = getWidgetBool(g_fscCheckSpeedbrakeRev);
    updated.fsc.debug = getWidgetBool(g_fscCheckDebug);
    updated.fsc.serial.dtr = getWidgetBool(g_fscCheckDtr);
    updated.fsc.serial.rts = getWidgetBool(g_fscCheckRts);
    updated.fsc.serial.xonxoff = getWidgetBool(g_fscCheckXonxoff);

    if (getWidgetBool(g_fscRadioTypeSemi)) {
        updated.fsc.type = Prefs::FscType::SemiPro;
    } else if (getWidgetBool(g_fscRadioTypePro)) {
        updated.fsc.type = Prefs::FscType::Pro;
    } else if (getWidgetBool(g_fscRadioTypeMotor)) {
        updated.fsc.type = Prefs::FscType::Motorized;
    }

    if (getWidgetBool(g_fscRadioParityEven)) {
        updated.fsc.serial.parity = Prefs::FscParity::Even;
    } else if (getWidgetBool(g_fscRadioParityOdd)) {
        updated.fsc.serial.parity = Prefs::FscParity::Odd;
    } else {
        updated.fsc.serial.parity = Prefs::FscParity::None;
    }

    std::string port = trimString(getWidgetText(g_fscFieldPort));
    if (!port.empty()) {
        updated.fsc.port = port;
    } else {
        logLine("FSC UI: port is empty; keeping previous value.");
    }

    int value = 0;
    if (parseIntField(getWidgetText(g_fscFieldBaud), value)) {
        updated.fsc.serial.baud = value;
    } else {
        logLine("FSC UI: invalid baud; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldDataBits), value)) {
        updated.fsc.serial.dataBits = value;
    } else {
        logLine("FSC UI: invalid data bits; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldStopBits), value)) {
        updated.fsc.serial.stopBits = value;
    } else {
        logLine("FSC UI: invalid stop bits; keeping previous value.");
    }

    if (parseIntField(getWidgetText(g_fscFieldThrottleSmoothMs), value)) {
        updated.fsc.throttleSmoothMs = value;
    } else {
        logLine("FSC UI: invalid throttle smooth ms; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldThrottleDeadband), value)) {
        updated.fsc.throttleDeadband = value;
    } else {
        logLine("FSC UI: invalid throttle deadband; keeping previous value.");
    }
    float fvalue = 0.0f;
    if (parseFloatField(getWidgetText(g_fscFieldThrottleSyncBand), fvalue)) {
        updated.fsc.throttleSyncBand = fvalue;
    } else {
        logLine("FSC UI: invalid throttle sync band; keeping previous value.");
    }

    if (parseIntField(getWidgetText(g_fscFieldMotorSpoilersDown), value)) {
        updated.fsc.motor.spoilersDown = value;
    } else {
        logLine("FSC UI: invalid motor spoilers down; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorSpoilersUp), value)) {
        updated.fsc.motor.spoilersUp = value;
    } else {
        logLine("FSC UI: invalid motor spoilers up; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorThrottle1Min), value)) {
        updated.fsc.motor.throttle1Min = value;
    } else {
        logLine("FSC UI: invalid motor throttle1 min; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorThrottle1Max), value)) {
        updated.fsc.motor.throttle1Max = value;
    } else {
        logLine("FSC UI: invalid motor throttle1 max; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorThrottle2Min), value)) {
        updated.fsc.motor.throttle2Min = value;
    } else {
        logLine("FSC UI: invalid motor throttle2 min; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorThrottle2Max), value)) {
        updated.fsc.motor.throttle2Max = value;
    } else {
        logLine("FSC UI: invalid motor throttle2 max; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorTrimArrow02), value)) {
        updated.fsc.motor.trimArrow02 = value;
    } else {
        logLine("FSC UI: invalid motor trim arrow 02; keeping previous value.");
    }
    if (parseIntField(getWidgetText(g_fscFieldMotorTrimArrow17), value)) {
        updated.fsc.motor.trimArrow17 = value;
    } else {
        logLine("FSC UI: invalid motor trim arrow 17; keeping previous value.");
    }
    if (parseFloatField(getWidgetText(g_fscFieldMotorTrimWheel02), fvalue)) {
        updated.fsc.motor.trimWheel02 = fvalue;
    } else {
        logLine("FSC UI: invalid motor trim wheel 02; keeping previous value.");
    }
    if (parseFloatField(getWidgetText(g_fscFieldMotorTrimWheel17), fvalue)) {
        updated.fsc.motor.trimWheel17 = fvalue;
    } else {
        logLine("FSC UI: invalid motor trim wheel 17; keeping previous value.");
    }
    if (parseFloatField(getWidgetText(g_fscFieldMotorThrottleRate), fvalue)) {
        updated.fsc.motor.throttleUpdateRateSec = fvalue;
    } else {
        logLine("FSC UI: invalid motor throttle update rate; keeping previous value.");
    }

    normalizeFscSerial(updated.fsc.serial);
    normalizeFscThrottleFilter(updated.fsc);

    if (!writeFscSettingsToPrefsFile(updated)) {
        logLine("FSC UI: failed to save settings to prefs.");
        return;
    }
    reloadPrefs();
    syncFscWindowFromPrefs();
}

static int fscWindowHandler(XPWidgetMessage inMessage, XPWidgetID inWidget, intptr_t inParam1, intptr_t inParam2) {
    if (XPUFixedLayout(inMessage, inWidget, inParam1, inParam2)) {
        return 1;
    }
    if (inMessage == xpMessage_CloseButtonPushed) {
        XPHideWidget(g_fscWindow);
        return 1;
    }
    if (inMessage == xpMsg_PushButtonPressed) {
        XPWidgetID widget = reinterpret_cast<XPWidgetID>(inParam1);
        if (widget == g_fscBtnSaveApply) {
            applyFscWindowSettings();
            return 1;
        }
        if (widget == g_fscBtnReload) {
            if (g_cmdReloadPrefs) {
                XPLMCommandOnce(g_cmdReloadPrefs);
            }
            return 1;
        }
        if (widget == g_fscBtnCalibStart) {
            if (g_cmdFscCalibStart) {
                XPLMCommandOnce(g_cmdFscCalibStart);
            }
            return 1;
        }
        if (widget == g_fscBtnCalibNext) {
            if (g_cmdFscCalibNext) {
                XPLMCommandOnce(g_cmdFscCalibNext);
            }
            return 1;
        }
        if (widget == g_fscBtnCalibCancel) {
            if (g_cmdFscCalibCancel) {
                XPLMCommandOnce(g_cmdFscCalibCancel);
            }
            return 1;
        }
        if (widget == g_fscBtnClose) {
            XPHideWidget(g_fscWindow);
            return 1;
        }
    }
    if (inMessage == xpMsg_ButtonStateChanged) {
        XPWidgetID widget = reinterpret_cast<XPWidgetID>(inParam1);
        int newState = static_cast<int>(inParam2);
        if (newState == 1) {
            if (widget == g_fscRadioTypeSemi || widget == g_fscRadioTypePro || widget == g_fscRadioTypeMotor) {
                setWidgetBool(g_fscRadioTypeSemi, widget == g_fscRadioTypeSemi);
                setWidgetBool(g_fscRadioTypePro, widget == g_fscRadioTypePro);
                setWidgetBool(g_fscRadioTypeMotor, widget == g_fscRadioTypeMotor);
                return 1;
            }
            if (widget == g_fscRadioParityNone || widget == g_fscRadioParityEven || widget == g_fscRadioParityOdd) {
                setWidgetBool(g_fscRadioParityNone, widget == g_fscRadioParityNone);
                setWidgetBool(g_fscRadioParityEven, widget == g_fscRadioParityEven);
                setWidgetBool(g_fscRadioParityOdd, widget == g_fscRadioParityOdd);
                return 1;
            }
        }
    }
    return 0;
}

static void ensureFscWindow() {
    if (g_fscWindow) {
        return;
    }
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetScreenBoundsGlobal(&l, &t, &r, &b);
    const int width = 820;
    const int height = 840;
    const int left = l + 80;
    const int top = t - 80;
    const int right = left + width;
    const int bottom = top - height;

    g_fscWindow = XPCreateWidget(left, top, right, bottom, 0, PLUGIN_MENU_TITLE, 1, nullptr, xpWidgetClass_MainWindow);
    XPSetWidgetProperty(g_fscWindow, xpProperty_MainWindowType, xpMainWindowStyle_Translucent);
    XPSetWidgetProperty(g_fscWindow, xpProperty_MainWindowHasCloseBoxes, 1);
    XPAddWidgetCallback(g_fscWindow, fscWindowHandler);

    g_fscPanel = XPCreateWidget(left + 10, top - 30, right - 10, bottom + 10, 1, "", 0, g_fscWindow, xpWidgetClass_SubWindow);
    XPSetWidgetProperty(g_fscPanel, xpProperty_SubWindowType, xpSubWindowStyle_SubWindow);

    const int rowHeight = 24;
    const int rowGap = 4;
    const int btnW = 180;
    const int btnH = 22;
    const int btnGap = 10;
    const int btnLeft = right - btnW - 20;
    const int contentRight = btnLeft - 20;
    const int labelX = left + 20;
    const int labelW = 180;
    const int fieldX = labelX + labelW + 14;
    int fieldW = contentRight - fieldX;
    if (fieldW < 140) {
        fieldW = 140;
    }
    const int smallFieldW = 120;
    const int tinyFieldW = 70;
    const int boxW = 18;
    const int boxLabelGap = 10;
    const int optionGap = 20;

    auto optionWidthForLabel = [&](const char* label, int minWidth) {
        int labelPx = static_cast<int>(XPLMMeasureString(
            xplmFont_Proportional,
            label,
            static_cast<int>(std::strlen(label))) + 0.5f);
        int width = labelPx + boxLabelGap + boxW;
        if (width < minWidth) {
            width = minWidth;
        }
        return width;
    };

    int y = top - 55;

    auto makeRadioRight = [&](int left, int right, const char* label) {
        int boxRight = right;
        int boxLeft = boxRight - boxW;
        int textRight = boxLeft - boxLabelGap;
        if (textRight < left + 20) {
            textRight = left + 20;
        }
        createCaption(left, y, textRight, y - rowHeight, label, g_fscWindow);
        return createRadioButton(boxLeft, y, boxRight, y - rowHeight, "", g_fscWindow);
    };
    auto makeCheckRight = [&](int left, int right, const char* label) {
        int boxRight = right;
        int boxLeft = boxRight - boxW;
        int textRight = boxLeft - boxLabelGap;
        if (textRight < left + 20) {
            textRight = left + 20;
        }
        createCaption(left, y, textRight, y - rowHeight, label, g_fscWindow);
        return createCheckbox(boxLeft, y, boxRight, y - rowHeight, "", g_fscWindow);
    };

    int enabledW = optionWidthForLabel("FSC enabled", 120);
    g_fscCheckEnabled = makeCheckRight(labelX, labelX + enabledW, "FSC enabled");
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Type:", g_fscWindow);
    int radioX = fieldX;
    const int typeSemiW = optionWidthForLabel("SEMIPRO", 90);
    const int typeProW = optionWidthForLabel("PRO", 70);
    const int typeMotorW = optionWidthForLabel("MOTORIZED", 110);
    g_fscRadioTypeSemi = makeRadioRight(radioX, radioX + typeSemiW, "SEMIPRO");
    radioX += typeSemiW + optionGap;
    g_fscRadioTypePro = makeRadioRight(radioX, radioX + typeProW, "PRO");
    radioX += typeProW + optionGap;
    int motorRight = radioX + typeMotorW;
    if (motorRight > contentRight) {
        motorRight = contentRight;
    }
    g_fscRadioTypeMotor = makeRadioRight(radioX, motorRight, "MOTORIZED");
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Port:", g_fscWindow);
    g_fscFieldPort = createTextField(fieldX, y, fieldX + fieldW, y - rowHeight, "", 64, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Baud:", g_fscWindow);
    g_fscFieldBaud = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Data bits:", g_fscWindow);
    g_fscFieldDataBits = createTextField(fieldX, y, fieldX + tinyFieldW, y - rowHeight, "", 2, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Stop bits:", g_fscWindow);
    g_fscFieldStopBits = createTextField(fieldX, y, fieldX + tinyFieldW, y - rowHeight, "", 2, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Parity:", g_fscWindow);
    radioX = fieldX;
    const int parityNoneW = optionWidthForLabel("None", 70);
    const int parityEvenW = optionWidthForLabel("Even", 70);
    const int parityOddW = optionWidthForLabel("Odd", 70);
    g_fscRadioParityNone = makeRadioRight(radioX, radioX + parityNoneW, "None");
    radioX += parityNoneW + optionGap;
    g_fscRadioParityEven = makeRadioRight(radioX, radioX + parityEvenW, "Even");
    radioX += parityEvenW + optionGap;
    g_fscRadioParityOdd = makeRadioRight(radioX, radioX + parityOddW, "Odd");
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Lines:", g_fscWindow);
    const int lineDtrW = optionWidthForLabel("DTR", 60);
    const int lineRtsW = optionWidthForLabel("RTS", 60);
    const int lineXonW = optionWidthForLabel("XON/XOFF", 110);
    g_fscCheckDtr = makeCheckRight(fieldX, fieldX + lineDtrW, "DTR");
    int rtsLeft = fieldX + lineDtrW + optionGap;
    g_fscCheckRts = makeCheckRight(rtsLeft, rtsLeft + lineRtsW, "RTS");
    int xonLeft = rtsLeft + lineRtsW + optionGap;
    int xonRight = xonLeft + lineXonW;
    if (xonRight > contentRight) {
        xonRight = contentRight;
    }
    g_fscCheckXonxoff = makeCheckRight(xonLeft, xonRight, "XON/XOFF");
    y -= (rowHeight + rowGap);

    int fuelInvertW = optionWidthForLabel("Fuel lever inverted", 170);
    g_fscCheckFuelInvert = makeCheckRight(labelX, labelX + fuelInvertW, "Fuel lever inverted");
    y -= (rowHeight + rowGap);

    int speedbrakeW = optionWidthForLabel("Speedbrake reversed", 180);
    g_fscCheckSpeedbrakeRev = makeCheckRight(labelX, labelX + speedbrakeW, "Speedbrake reversed");
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Throttle smoothing (ms):", g_fscWindow);
    g_fscFieldThrottleSmoothMs = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Throttle deadband:", g_fscWindow);
    g_fscFieldThrottleDeadband = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Throttle sync band:", g_fscWindow);
    g_fscFieldThrottleSyncBand = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    int debugW = optionWidthForLabel("Debug", 80);
    g_fscCheckDebug = makeCheckRight(labelX, labelX + debugW, "Debug");
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, contentRight, y - rowHeight, "Motorized (advanced):", g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor spoilers down:", g_fscWindow);
    g_fscFieldMotorSpoilersDown = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor spoilers up:", g_fscWindow);
    g_fscFieldMotorSpoilersUp = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor throttle 1 min:", g_fscWindow);
    g_fscFieldMotorThrottle1Min = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor throttle 1 max:", g_fscWindow);
    g_fscFieldMotorThrottle1Max = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor throttle 2 min:", g_fscWindow);
    g_fscFieldMotorThrottle2Min = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor throttle 2 max:", g_fscWindow);
    g_fscFieldMotorThrottle2Max = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor trim arrow 0.2:", g_fscWindow);
    g_fscFieldMotorTrimArrow02 = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor trim arrow 1.7:", g_fscWindow);
    g_fscFieldMotorTrimArrow17 = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor trim wheel 0.2:", g_fscWindow);
    g_fscFieldMotorTrimWheel02 = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor trim wheel 1.7:", g_fscWindow);
    g_fscFieldMotorTrimWheel17 = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    createCaption(labelX, y, labelX + labelW, y - rowHeight, "Motor throttle update (sec):", g_fscWindow);
    g_fscFieldMotorThrottleRate = createTextField(fieldX, y, fieldX + smallFieldW, y - rowHeight, "", 10, g_fscWindow);
    y -= (rowHeight + rowGap);

    g_fscCalibStatus = createCaption(labelX, y, contentRight, y - rowHeight, g_fscCalibStatusText.c_str(), g_fscWindow);

    int bx = btnLeft;
    int by = top - 55;
    g_fscBtnSaveApply = createPushButton(bx, by, bx + btnW, by - btnH, "Save & Apply", g_fscWindow);
    by -= (btnH + btnGap);
    g_fscBtnReload = createPushButton(bx, by, bx + btnW, by - btnH, "Reload prefs", g_fscWindow);
    by -= (btnH + btnGap);
    g_fscBtnCalibStart = createPushButton(bx, by, bx + btnW, by - btnH, "Calibration: Start", g_fscWindow);
    by -= (btnH + btnGap);
    g_fscBtnCalibNext = createPushButton(bx, by, bx + btnW, by - btnH, "Calibration: Next", g_fscWindow);
    by -= (btnH + btnGap);
    g_fscBtnCalibCancel = createPushButton(bx, by, bx + btnW, by - btnH, "Calibration: Cancel", g_fscWindow);
    g_fscBtnClose = createPushButton(bx, y, bx + btnW, y - btnH, "Close", g_fscWindow);

    syncFscWindowFromPrefs();
}

static void toggleFscWindow() {
    ensureFscWindow();
    if (!g_fscWindow) {
        return;
    }
    if (XPIsWidgetVisible(g_fscWindow)) {
        XPHideWidget(g_fscWindow);
    } else {
        syncFscWindowFromPrefs();
        XPShowWidget(g_fscWindow);
    }
}

static void destroyFscWindow() {
    if (!g_fscWindow) {
        return;
    }
    XPDestroyWidget(g_fscWindow, 1);
    g_fscWindow = nullptr;
    g_fscPanel = nullptr;
    g_fscCheckEnabled = nullptr;
    g_fscRadioTypeSemi = nullptr;
    g_fscRadioTypePro = nullptr;
    g_fscRadioTypeMotor = nullptr;
    g_fscFieldPort = nullptr;
    g_fscFieldBaud = nullptr;
    g_fscFieldDataBits = nullptr;
    g_fscFieldStopBits = nullptr;
    g_fscRadioParityNone = nullptr;
    g_fscRadioParityEven = nullptr;
    g_fscRadioParityOdd = nullptr;
    g_fscCheckDtr = nullptr;
    g_fscCheckRts = nullptr;
    g_fscCheckXonxoff = nullptr;
    g_fscCheckFuelInvert = nullptr;
    g_fscCheckSpeedbrakeRev = nullptr;
    g_fscFieldThrottleSmoothMs = nullptr;
    g_fscFieldThrottleDeadband = nullptr;
    g_fscFieldThrottleSyncBand = nullptr;
    g_fscCheckDebug = nullptr;
    g_fscFieldMotorSpoilersDown = nullptr;
    g_fscFieldMotorSpoilersUp = nullptr;
    g_fscFieldMotorThrottle1Min = nullptr;
    g_fscFieldMotorThrottle1Max = nullptr;
    g_fscFieldMotorThrottle2Min = nullptr;
    g_fscFieldMotorThrottle2Max = nullptr;
    g_fscFieldMotorTrimArrow02 = nullptr;
    g_fscFieldMotorTrimArrow17 = nullptr;
    g_fscFieldMotorTrimWheel02 = nullptr;
    g_fscFieldMotorTrimWheel17 = nullptr;
    g_fscFieldMotorThrottleRate = nullptr;
    g_fscCalibStatus = nullptr;
    g_fscBtnSaveApply = nullptr;
    g_fscBtnReload = nullptr;
    g_fscBtnCalibStart = nullptr;
    g_fscBtnCalibNext = nullptr;
    g_fscBtnCalibCancel = nullptr;
    g_fscBtnClose = nullptr;
}

static void menuHandler(void* /*inMenuRef*/, void* inItemRef) {
    if (inItemRef == kMenuToggleWindow) {
        toggleFscWindow();
        return;
    }
    auto cmd = reinterpret_cast<XPLMCommandRef>(inItemRef);
    if (cmd) {
        XPLMCommandOnce(cmd);
    }
}

static void createPluginMenu() {
    if (g_menuId) {
        return;
    }
    XPLMMenuID pluginsMenu = XPLMFindPluginsMenu();
    if (!pluginsMenu) {
        return;
    }
    g_menuBaseItem = XPLMAppendMenuItem(pluginsMenu, PLUGIN_MENU_TITLE, nullptr, 1);
    g_menuId = XPLMCreateMenu(PLUGIN_MENU_TITLE, pluginsMenu, g_menuBaseItem, menuHandler, nullptr);
    if (!g_menuId) {
        return;
    }
    g_menuToggleItem = XPLMAppendMenuItem(g_menuId, "Open Window", kMenuToggleWindow, 1);
    XPLMAppendMenuItem(g_menuId, "Reload prefs", g_cmdReloadPrefs, 1);
    XPLMAppendMenuItem(g_menuId, "Calibration: Start", g_cmdFscCalibStart, 1);
    XPLMAppendMenuItem(g_menuId, "Calibration: Next", g_cmdFscCalibNext, 1);
    XPLMAppendMenuItem(g_menuId, "Calibration: Cancel", g_cmdFscCalibCancel, 1);
}

static void destroyPluginMenu() {
    if (!g_menuId) {
        return;
    }
    XPLMDestroyMenu(g_menuId);
    g_menuId = nullptr;
    g_menuBaseItem = -1;
    g_menuToggleItem = -1;
}

static bool updatePrefsFile(const std::unordered_map<std::string, std::string>& kv,
                            const std::string& logPrefix,
                            const std::string& successMsg) {
    const std::string path = getPrefsPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        logLine(logPrefix + ": failed to open prefs for write: " + path);
        return false;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    in.close();

    // Backup
    {
        std::ofstream bak(path + ".bak", std::ios::trunc);
        if (bak.is_open()) {
            for (const auto& l : lines) {
                bak << l << "\n";
            }
        }
    }

    std::unordered_set<std::string> updated;
    for (auto& l : lines) {
        if (l.empty() || l[0] == '#') {
            continue;
        }
        auto pos = l.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trimString(l.substr(0, pos));
        auto it = kv.find(key);
        if (it != kv.end()) {
            l = key + "=" + it->second;
            updated.insert(key);
        }
    }
    for (const auto& it : kv) {
        if (updated.find(it.first) == updated.end()) {
            lines.push_back(it.first + "=" + it.second);
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        logLine(logPrefix + ": failed to write prefs: " + path);
        return false;
    }
    for (const auto& l : lines) {
        out << l << "\n";
    }
    logLine(logPrefix + ": " + successMsg + " (backup at " + path + ".bak)");
    return true;
}

static bool writeFscSettingsToPrefsFile(const Prefs& prefs) {
    std::unordered_map<std::string, std::string> kv = {
        {"fsc.enabled", prefs.fsc.enabled ? "1" : "0"},
        {"fsc.port", prefs.fsc.port},
        {"fsc.type", fscTypeToString(prefs.fsc.type)},
        {"fsc.speed_brake_reversed", prefs.fsc.speedBrakeReversed ? "1" : "0"},
        {"fsc.fuel_lever_inverted", prefs.fsc.fuelLeverInverted ? "1" : "0"},
        {"fsc.throttle_smooth_ms", std::to_string(prefs.fsc.throttleSmoothMs)},
        {"fsc.throttle_deadband", std::to_string(prefs.fsc.throttleDeadband)},
        {"fsc.throttle_sync_band", std::to_string(prefs.fsc.throttleSyncBand)},
        {"fsc.debug", prefs.fsc.debug ? "1" : "0"},
        {"fsc.baud", std::to_string(prefs.fsc.serial.baud)},
        {"fsc.data_bits", std::to_string(prefs.fsc.serial.dataBits)},
        {"fsc.stop_bits", std::to_string(prefs.fsc.serial.stopBits)},
        {"fsc.parity", fscParityToString(prefs.fsc.serial.parity)},
        {"fsc.dtr", prefs.fsc.serial.dtr ? "1" : "0"},
        {"fsc.rts", prefs.fsc.serial.rts ? "1" : "0"},
        {"fsc.xonxoff", prefs.fsc.serial.xonxoff ? "1" : "0"},
    };
    return updatePrefsFile(kv, "FSC UI", "prefs updated with settings");
}

static bool writeFscCalibToPrefsFile(const Prefs& prefs) {
    const auto& c = prefs.fsc.calib;
    std::unordered_map<std::string, std::string> kv = {
        {"fsc.speed_brake_reversed", prefs.fsc.speedBrakeReversed ? "1" : "0"},
        {"fsc.calib.spoilers_down", std::to_string(c.spoilersDown)},
        {"fsc.calib.spoilers_armed", std::to_string(c.spoilersArmed)},
        {"fsc.calib.spoilers_min", std::to_string(c.spoilersMin)},
        {"fsc.calib.spoilers_detent", std::to_string(c.spoilersDetent)},
        {"fsc.calib.spoilers_up", std::to_string(c.spoilersUp)},
        {"fsc.calib.throttle1_min", std::to_string(c.throttle1Min)},
        {"fsc.calib.throttle1_full", std::to_string(c.throttle1Full)},
        {"fsc.calib.throttle2_min", std::to_string(c.throttle2Min)},
        {"fsc.calib.throttle2_full", std::to_string(c.throttle2Full)},
        {"fsc.calib.reverser1_min", std::to_string(c.reverser1Min)},
        {"fsc.calib.reverser1_max", std::to_string(c.reverser1Max)},
        {"fsc.calib.reverser2_min", std::to_string(c.reverser2Min)},
        {"fsc.calib.reverser2_max", std::to_string(c.reverser2Max)},
    };
    if (prefs.fsc.type == Prefs::FscType::SemiPro) {
        kv["fsc.calib.flaps_00"] = std::to_string(c.flaps00);
        kv["fsc.calib.flaps_01"] = std::to_string(c.flaps01);
        kv["fsc.calib.flaps_02"] = std::to_string(c.flaps02);
        kv["fsc.calib.flaps_05"] = std::to_string(c.flaps05);
        kv["fsc.calib.flaps_10"] = std::to_string(c.flaps10);
        kv["fsc.calib.flaps_15"] = std::to_string(c.flaps15);
        kv["fsc.calib.flaps_25"] = std::to_string(c.flaps25);
        kv["fsc.calib.flaps_30"] = std::to_string(c.flaps30);
        kv["fsc.calib.flaps_40"] = std::to_string(c.flaps40);
    }
    return updatePrefsFile(kv, "FSC CAL", "prefs updated with calibration");
}

static void logFscSettings() {
    if (!g_prefs.fsc.enabled) {
        return;
    }
    logLine("FSC enabled: type=" + fscTypeToString(g_prefs.fsc.type) + ", port=" + g_prefs.fsc.port +
            ", " + fscSerialSummary(g_prefs.fsc.serial) +
            ", speed_brake_reversed=" + std::string(g_prefs.fsc.speedBrakeReversed ? "1" : "0") +
            ", fuel_lever_inverted=" + std::string(g_prefs.fsc.fuelLeverInverted ? "1" : "0") +
            ", throttle_smooth_ms=" + std::to_string(g_prefs.fsc.throttleSmoothMs) +
            ", throttle_deadband=" + std::to_string(g_prefs.fsc.throttleDeadband) +
            ", throttle_sync_band=" + std::to_string(g_prefs.fsc.throttleSyncBand) +
            ", debug=" + std::string(g_prefs.fsc.debug ? "1" : "0"));
}

static void maybeRunFscDeferredInit() {
    if (!g_fscDeferredInit.pending) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < g_fscDeferredInit.due) {
        return;
    }

    constexpr int kMaxAttempts = 20;
    constexpr auto kRetryDelay = std::chrono::seconds(5);

    if (!g_fscProfileValid.load()) {
        g_fscDeferredInit.pending = false;
        return;
    }

    if (g_fscProfileId.empty()) {
        if (refreshFscProfile(true)) {
            updateFscLifecycle("fsc deferred select");
            g_fscDeferredInit.pending = false;
            g_fscDeferredInit.attempts = 0;
            return;
        }
        if (++g_fscDeferredInit.attempts >= kMaxAttempts) {
            g_fscDeferredInit.pending = false;
            logLine("FSC deferred init: no profile match, giving up.");
            return;
        }
        g_fscDeferredInit.due = now + kRetryDelay;
        return;
    }

    bool missingRefs = false;
    bool ready = resolveFscProfileBindings(true, missingRefs);
    if (ready || !g_fscProfileRuntime.sync.deferUntilDatarefs) {
        g_fscProfileActive.store(true);
        if (g_fscProfileRuntime.sync.resyncOnAircraftLoaded) {
            g_fscResyncPending.store(true);
            scheduleFscAxisResync();
        }
        g_fscLastResync = now;
        g_fscDeferredInit.pending = false;
        g_fscDeferredInit.attempts = 0;
        logLine("FSC deferred init: bindings ready for profile '" + g_fscProfileId + "'");
        updateFscLifecycle("fsc deferred init");
        return;
    }

    if (++g_fscDeferredInit.attempts >= kMaxAttempts) {
        g_fscDeferredInit.pending = false;
        logLine("FSC deferred init: bindings still missing, giving up.");
        return;
    }
    g_fscDeferredInit.due = now + kRetryDelay;
}

float flightLoopCallback(
    float /*inElapsedSinceLastCall*/,
    float /*inElapsedTimeSinceLastFlightLoop*/,
    int /*inCounter*/,
    void* /*inRefcon*/) {
    maybeRunFscDeferredInit();

    if (g_prefs.fsc.enabled) {
        FscState snapshot;
        {
            std::lock_guard<std::mutex> lock(g_fscMutex);
            snapshot = g_fscState;
            g_fscState.trimWheelDelta = 0;
        }
        bool calibActive = false;
        {
            std::lock_guard<std::mutex> lock(g_fscCalibMutex);
            calibActive = g_fscCalib.active;
        }
        if (calibActive) {
            updateFscCalibration(snapshot);
        } else if (g_fscProfileActive.load()) {
            auto now = std::chrono::steady_clock::now();
            if (g_fscResyncPending.load()) {
                if (snapshot.digital >= 0 || snapshot.stabTrim >= 0) {
                    resyncFscLatchingInputs(snapshot);
                    g_fscLastResync = now;
                    g_fscResyncPending.store(false);
                }
            } else if (g_fscProfileRuntime.sync.resyncIntervalSec > 0.0f) {
                auto interval = std::chrono::duration<float>(g_fscProfileRuntime.sync.resyncIntervalSec);
                if (g_fscLastResync.time_since_epoch().count() == 0 ||
                    now - g_fscLastResync >= interval) {
                    if (snapshot.digital >= 0 || snapshot.stabTrim >= 0) {
                        resyncFscLatchingInputs(snapshot);
                        g_fscLastResync = now;
                    }
                }
            }
            if (g_fscAxisResyncPending.load() && now >= g_fscAxisResyncDue) {
                resyncFscDetentAxes();
                g_fscAxisResyncPending.store(false);
            }
            processFscOutputs(snapshot);
            processFscState(snapshot);
        }
    }

    return -1.0f;  // next frame
}

float clamp01(float v) {
    if (v < 0.f) return 0.f;
    if (v > 1.f) return 1.f;
    return v;
}

#if IBM
static std::string win32ErrorMessage(DWORD err) {
    if (err == 0) {
        return {};
    }
    LPSTR msgBuf = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msgBuf),
        0,
        nullptr);
    std::string msg;
    if (size != 0 && msgBuf) {
        msg.assign(msgBuf, size);
        LocalFree(msgBuf);
    } else {
        msg = "unknown";
    }
    while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' ' || msg.back() == '\t')) {
        msg.pop_back();
    }
    return msg;
}
#endif

static void closeFscPort(intptr_t handle) {
#if IBM
    HANDLE h = reinterpret_cast<HANDLE>(handle);
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
#else
    int fd = static_cast<int>(handle);
    if (fd >= 0) {
        ::close(fd);
    }
#endif
}

#if !IBM
static speed_t fscBaudToTermios(int baud, bool& ok) {
    ok = true;
    switch (baud) {
        case 1200: return B1200;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
    }
    ok = false;
    return B115200;
}

static void applyFscControlLines(int fd, const Prefs::FscSerial& serial) {
    int status = 0;
    if (ioctl(fd, TIOCMGET, &status) != 0) {
        return;
    }
    if (serial.dtr) status |= TIOCM_DTR;
    else status &= ~TIOCM_DTR;
    if (serial.rts) status |= TIOCM_RTS;
    else status &= ~TIOCM_RTS;
    ioctl(fd, TIOCMSET, &status);
}
#endif

static intptr_t openFscPort(const std::string& port, const Prefs::FscSerial& serial) {
#if IBM
    std::string device = port;
    // Accept COM3, COM10, and \\.\COM10 formats.
    if (device.rfind("\\\\.", 0) != 0 && device.rfind("COM", 0) == 0) {
        int num = 0;
        try {
            num = std::stoi(device.substr(3));
        } catch (...) {
            num = 0;
        }
        if (num >= 10) {
            device = "\\\\.\\" + device;
        }
    }

    HANDLE h = CreateFileA(
        device.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }

    SetupComm(h, 4096, 4096);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return -1;
    }

    int baud = serial.baud > 0 ? serial.baud : 115200;
    int dataBits = serial.dataBits;
    if (dataBits < 5 || dataBits > 8) {
        dataBits = 8;
    }
    int stopBits = (serial.stopBits == 2) ? 2 : 1;
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = static_cast<BYTE>(dataBits);
    switch (serial.parity) {
        case Prefs::FscParity::Even:
            dcb.Parity = EVENPARITY;
            dcb.fParity = TRUE;
            break;
        case Prefs::FscParity::Odd:
            dcb.Parity = ODDPARITY;
            dcb.fParity = TRUE;
            break;
        case Prefs::FscParity::None:
        default:
            dcb.Parity = NOPARITY;
            dcb.fParity = FALSE;
            break;
    }
    dcb.StopBits = stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = serial.dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = serial.rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = serial.xonxoff ? TRUE : FALSE;
    dcb.fInX = serial.xonxoff ? TRUE : FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.XonChar = 0x11;
    dcb.XoffChar = 0x13;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return -1;
    }

    return reinterpret_cast<intptr_t>(h);
#else
    int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }
    termios t{};
    if (tcgetattr(fd, &t) != 0) {
        ::close(fd);
        return -1;
    }
    cfmakeraw(&t);
    bool baudOk = true;
    speed_t baud = fscBaudToTermios(serial.baud, baudOk);
    if (!baudOk) {
        logLine("FSC: unsupported baud " + std::to_string(serial.baud) + ", using 115200");
    }
    cfsetispeed(&t, baud);
    cfsetospeed(&t, baud);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CSIZE;
    int dataBits = serial.dataBits;
    if (dataBits < 5 || dataBits > 8) {
        dataBits = 8;
    }
    switch (dataBits) {
        case 5: t.c_cflag |= CS5; break;
        case 6: t.c_cflag |= CS6; break;
        case 7: t.c_cflag |= CS7; break;
        case 8: default: t.c_cflag |= CS8; break;
    }
    if (serial.stopBits == 2) t.c_cflag |= CSTOPB;
    else t.c_cflag &= ~CSTOPB;
    if (serial.parity == Prefs::FscParity::Even) {
        t.c_cflag |= PARENB;
        t.c_cflag &= ~PARODD;
    } else if (serial.parity == Prefs::FscParity::Odd) {
        t.c_cflag |= PARENB;
        t.c_cflag |= PARODD;
    } else {
        t.c_cflag &= ~PARENB;
    }
    if (serial.xonxoff) {
        t.c_iflag |= (IXON | IXOFF);
    } else {
        t.c_iflag &= ~(IXON | IXOFF | IXANY);
    }
    t.c_cc[VTIME] = 1;
    t.c_cc[VMIN] = 0;
    if (tcsetattr(fd, TCSANOW, &t) != 0) {
        ::close(fd);
        return -1;
    }
    applyFscControlLines(fd, serial);
    return fd;
#endif
}

int readByteWithTimeout(intptr_t handle, uint8_t& out, int timeoutMs) {
    if (handle < 0) {
        errno = EBADF;
        return -1;
    }
#if IBM
    HANDLE h = reinterpret_cast<HANDLE>(handle);
    if (!h || h == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return -1;
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(timeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    if (!SetCommTimeouts(h, &timeouts)) {
        return -1;
    }
    DWORD bytesRead = 0;
    if (!ReadFile(h, &out, 1, &bytesRead, nullptr)) {
        return -1;
    }
    return bytesRead == 1 ? 1 : 0;
#else
    int fd = static_cast<int>(handle);
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int ret = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (ret == 0) {
        return 0;  // timeout
    }
    if (ret < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    ssize_t n = ::read(fd, &out, 1);
    return n == 1 ? 1 : -1;
#endif
}

static bool fscWriteBytes(intptr_t handle, const uint8_t* data, size_t len) {
    if (handle < 0) {
        errno = EBADF;
        return false;
    }
#if IBM
    HANDLE h = reinterpret_cast<HANDLE>(handle);
    DWORD written = 0;
    if (!WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr)) {
        return false;
    }
    return written == len;
#else
    int fd = static_cast<int>(handle);
    ssize_t n = ::write(fd, data, len);
    return n == static_cast<ssize_t>(len);
#endif
}

void fscSendPoll() {
    const uint8_t frame[3] = {0x93, 0x00, 0x10};
    std::lock_guard<std::mutex> lock(g_fscIoMutex);
    intptr_t handle = g_fscFd.load();
    if (handle < 0) {
        return;
    }
    if (!fscWriteBytes(handle, frame, sizeof(frame))) {
#if IBM
        DWORD e = GetLastError();
        logLine("FSC: write poll failed (" + std::to_string(e) + "): " + win32ErrorMessage(e));
#else
        int e = errno;
        logLine("FSC: write poll failed (" + std::to_string(e) + "): " + std::strerror(e));
#endif
        closeFscPort(handle);
        g_fscFd.store(-1);
    }
}

void fscWriteFrame(uint8_t a, uint8_t b, uint8_t c) {
    const uint8_t frame[3] = {a, b, c};
    std::lock_guard<std::mutex> lock(g_fscIoMutex);
    intptr_t handle = g_fscFd.load();
    if (handle < 0) {
        return;
    }
    if (!fscWriteBytes(handle, frame, sizeof(frame))) {
#if IBM
        DWORD e = GetLastError();
        logLine("FSC: write failed (" + std::to_string(e) + "): " + win32ErrorMessage(e));
#else
        int e = errno;
        logLine("FSC: write failed (" + std::to_string(e) + "): " + std::strerror(e));
#endif
        closeFscPort(handle);
        g_fscFd.store(-1);
    }
}

void fscWritePosition(uint8_t base, int value0to255) {
    if (value0to255 < 0) value0to255 = 0;
    if (value0to255 > 255) value0to255 = 255;
    uint8_t msb = static_cast<uint8_t>((value0to255 >= 128) ? 1 : 0);
    uint8_t low7 = static_cast<uint8_t>(value0to255 & 0x7F);
    fscWriteFrame(0x8b, static_cast<uint8_t>(base | msb), low7);
}

void handleFscPacket(uint8_t cmdByte, uint8_t dataByte) {
    uint8_t cmd = static_cast<uint8_t>(cmdByte & 0x7E);  // strip MSB, keep command bits (1..6)
    uint8_t msb = static_cast<uint8_t>(cmdByte & 0x01);
    uint8_t y = static_cast<uint8_t>(dataByte & 0x7F);
    uint8_t value = static_cast<uint8_t>((msb << 7) | y);

    static int lastTrimAB = -1;

    std::lock_guard<std::mutex> lock(g_fscMutex);
    switch (cmd) {
        case 0x12:  // digital inputs (active low)
            g_fscState.digital = value;
            break;
        case 0x16:  // stab trim switches (bitfield in y)
            g_fscState.stabTrim = value;
            break;
        case 0x20:  // reverser 1
            g_fscState.reverser1 = value;
            break;
        case 0x22:  // reverser 2
            g_fscState.reverser2 = value;
            break;
        case 0x24:  // throttle 1
            g_fscState.throttle1 = value;
            break;
        case 0x26:  // throttle 2
            g_fscState.throttle2 = value;
            break;
        case 0x2A:  // flaps (semi-pro)
            if (g_prefs.fsc.type == Prefs::FscType::SemiPro) {
                g_fscState.flaps = value;
            }
            break;
        case 0x10: {  // flaps + trim wheel (pro/motorized)
            if (g_prefs.fsc.type == Prefs::FscType::Pro || g_prefs.fsc.type == Prefs::FscType::Motorized) {
                uint8_t flapCode = static_cast<uint8_t>(y & 0x0F);
                int flapSetting = -1;
                switch (flapCode) {
                    case 0x8: flapSetting = 0; break;   // 1000
                    case 0xC: flapSetting = 1; break;   // 1100
                    case 0xE: flapSetting = 2; break;   // 1110
                    case 0xF: flapSetting = 5; break;   // 1111
                    case 0x7: flapSetting = 10; break;  // 0111
                    case 0x3: flapSetting = 15; break;  // 0011
                    case 0x2: flapSetting = 25; break;  // 0010
                    case 0x1: flapSetting = 30; break;  // 0001
                    case 0x9: flapSetting = 40; break;  // 1001
                    default: break;
                }
                if (flapSetting >= 0) {
                    g_fscState.flaps = flapSetting;
                }

                int a = (y >> 4) & 0x01;
                int b = (y >> 5) & 0x01;
                int ab = (a << 1) | b;
                if (lastTrimAB >= 0 && ab != lastTrimAB) {
                    // Gray code quadrature decode
                    int delta = 0;
                    switch ((lastTrimAB << 2) | ab) {
                        case 0b0001:
                        case 0b0111:
                        case 0b1110:
                        case 0b1000:
                            delta = +1;
                            break;
                        case 0b0010:
                        case 0b1011:
                        case 0b1101:
                        case 0b0100:
                            delta = -1;
                            break;
                        default:
                            delta = 0;
                            break;
                    }
                    g_fscState.trimWheelDelta += delta;
                }
                lastTrimAB = ab;
            }
            break;
        }
        case 0x2C:  // speedbrake
            g_fscState.speedbrake = value;
            break;
        default:
            break;
    }
}

static bool decodeFscActiveLow(int digital, int mask, bool invert) {
    bool active = (digital & mask) == 0;
    return invert ? !active : active;
}

static bool readDatarefValue(XPLMDataRef ref, int type, float& out, int index = -1) {
    if (!ref) {
        return false;
    }
    if (type & xplmType_Float) {
        out = XPLMGetDataf(ref);
        return true;
    }
    if (type & xplmType_Double) {
        out = static_cast<float>(XPLMGetDatad(ref));
        return true;
    }
    if (type & xplmType_Int) {
        out = static_cast<float>(XPLMGetDatai(ref));
        return true;
    }
    if ((type & xplmType_FloatArray) && index >= 0) {
        float value = 0.0f;
        XPLMGetDatavf(ref, &value, index, 1);
        out = value;
        return true;
    }
    if ((type & xplmType_IntArray) && index >= 0) {
        int value = 0;
        XPLMGetDatavi(ref, &value, index, 1);
        out = static_cast<float>(value);
        return true;
    }
    return false;
}

static bool setDatarefValue(XPLMDataRef ref, int type, float value, int index = -1) {
    if (!ref) {
        return false;
    }
    if (type & xplmType_Float) {
        XPLMSetDataf(ref, value);
        return true;
    }
    if (type & xplmType_Double) {
        XPLMSetDatad(ref, static_cast<double>(value));
        return true;
    }
    if (type & xplmType_Int) {
        XPLMSetDatai(ref, static_cast<int>(std::lround(value)));
        return true;
    }
    if ((type & xplmType_FloatArray) && index >= 0) {
        float v = value;
        XPLMSetDatavf(ref, &v, index, 1);
        return true;
    }
    if ((type & xplmType_IntArray) && index >= 0) {
        int v = static_cast<int>(std::lround(value));
        XPLMSetDatavi(ref, &v, index, 1);
        return true;
    }
    return false;
}

static void executeAction(const FscAction& action) {
    if (action.type == FscAction::Type::Command) {
        if (!action.cmd) {
            return;
        }
        switch (action.phase) {
            case FscAction::Phase::Once:
                XPLMCommandOnce(action.cmd);
                break;
            case FscAction::Phase::Begin:
                XPLMCommandBegin(action.cmd);
                break;
            case FscAction::Phase::End:
                XPLMCommandEnd(action.cmd);
                break;
        }
        return;
    }
    if (action.type == FscAction::Type::Dataref) {
        if (!action.dataref) {
            return;
        }
        setDatarefValue(action.dataref, action.datarefType, action.value, action.index);
    }
}

static void executeActions(const std::vector<FscAction>& actions) {
    for (const auto& action : actions) {
        executeAction(action);
    }
}

static std::optional<bool> readSwitchValue(FscSwitchId id, const FscState& state) {
    switch (id) {
        case FscSwitchId::FuelCutoff1:
            if (state.digital < 0) return std::nullopt;
            return decodeFscActiveLow(state.digital, 0x01, g_prefs.fsc.fuelLeverInverted);
        case FscSwitchId::FuelCutoff2:
            if (state.digital < 0) return std::nullopt;
            return decodeFscActiveLow(state.digital, 0x02, g_prefs.fsc.fuelLeverInverted);
        case FscSwitchId::TogaLeft:
            if (state.digital < 0) return std::nullopt;
            return (state.digital & 0x04) == 0;
        case FscSwitchId::AutothrottleDisengage:
            if (state.digital < 0) return std::nullopt;
            return (state.digital & 0x08) == 0;
        case FscSwitchId::ParkingBrake:
            if (state.digital < 0) return std::nullopt;
            return (state.digital & 0x10) == 0;
        case FscSwitchId::GearHornCutout:
            if (state.digital < 0) return std::nullopt;
            return (state.digital & 0x20) == 0;
        case FscSwitchId::ElTrimGuard:
            if (state.stabTrim < 0) return std::nullopt;
            return (state.stabTrim & 0x02) != 0;
        case FscSwitchId::ApTrimGuard:
            if (state.stabTrim < 0) return std::nullopt;
            return (state.stabTrim & 0x04) != 0;
        case FscSwitchId::PitchTrimWheel:
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

static bool resyncFscLatchingInputs(const FscState& state) {
    if (!g_fscProfileActive.load()) {
        return false;
    }
    bool didResync = false;
    for (size_t i = 0; i < g_fscProfileRuntime.switches.size(); ++i) {
        const auto& mapping = g_fscProfileRuntime.switches[i];
        if (!mapping.defined) {
            continue;
        }
        if (mapping.type != FscSwitchMapping::Type::Latching &&
            mapping.type != FscSwitchMapping::Type::LatchingToggle) {
            continue;
        }
        auto valueOpt = readSwitchValue(static_cast<FscSwitchId>(i), state);
        if (!valueOpt.has_value()) {
            continue;
        }
        bool value = *valueOpt;
        bool shouldSend = true;
        if (mapping.type == FscSwitchMapping::Type::LatchingToggle && mapping.stateDataref) {
            float simValue = 0.0f;
            if (readDatarefValue(mapping.stateDataref, mapping.stateDatarefType, simValue)) {
                bool simOn = simValue >= mapping.stateOnMin;
                if (simOn == value) {
                    shouldSend = false;
                }
            }
        }
        if (shouldSend) {
            executeActions(value ? mapping.onActions : mapping.offActions);
        }
        g_fscSwitchState[i].known = true;
        g_fscSwitchState[i].value = value;
        didResync = true;
    }
    if (didResync) {
        logLine("FSC: resynced latching switches");
    }
    return didResync;
}

static void scheduleFscAxisResync() {
    g_fscAxisResyncPending.store(true);
    float delay = g_fscProfileRuntime.sync.startupDelaySec;
    if (delay < 0.0f) {
        delay = 0.0f;
    }
    auto offset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(delay));
    g_fscAxisResyncDue = std::chrono::steady_clock::now() + offset;
}

static bool resyncFscDetentAxes() {
    if (!g_fscProfileActive.load()) {
        return false;
    }
    bool didResync = false;
    if (g_fscProfileRuntime.flaps.enabled) {
        g_fscPrev.flaps = -1;
        didResync = true;
    }
    if (g_fscProfileRuntime.speedbrake.enabled) {
        bool motorizedHw = (g_prefs.fsc.type == Prefs::FscType::Motorized);
        if (!(motorizedHw && g_fscProfileRuntime.motorized.enabled)) {
            g_fscPrev.speedbrakeState = -1;
            g_fscPrev.speedbrakePrev = -1;
            didResync = true;
        }
    }
    if (didResync) {
        logLine("FSC: resynced detent axes");
    }
    return didResync;
}

void processFscState(const FscState& state) {
    if (!g_fscProfileActive.load()) {
        return;
    }

    auto handleSwitchChange = [&](FscSwitchId id, bool value) {
        size_t idx = static_cast<size_t>(id);
        const auto& mapping = g_fscProfileRuntime.switches[idx];
        if (!mapping.defined || mapping.type == FscSwitchMapping::Type::Encoder) {
            return;
        }
        auto& prev = g_fscSwitchState[idx];
        if (prev.known && prev.value == value) {
            return;
        }
        if (mapping.type == FscSwitchMapping::Type::Latching) {
            executeActions(value ? mapping.onActions : mapping.offActions);
        } else if (mapping.type == FscSwitchMapping::Type::LatchingToggle) {
            bool shouldSend = true;
            if (mapping.stateDataref) {
                float simValue = 0.0f;
                if (readDatarefValue(mapping.stateDataref, mapping.stateDatarefType, simValue)) {
                    bool simOn = simValue >= mapping.stateOnMin;
                    if (simOn == value) {
                        shouldSend = false;
                    }
                }
            }
            if (shouldSend) {
                executeActions(value ? mapping.onActions : mapping.offActions);
            }
        } else if (mapping.type == FscSwitchMapping::Type::Momentary) {
            executeActions(value ? mapping.pressActions : mapping.releaseActions);
        }
        prev.known = true;
        prev.value = value;
    };

    if (state.digital >= 0) {
        handleSwitchChange(FscSwitchId::FuelCutoff1, decodeFscActiveLow(state.digital, 0x01, g_prefs.fsc.fuelLeverInverted));
        handleSwitchChange(FscSwitchId::FuelCutoff2, decodeFscActiveLow(state.digital, 0x02, g_prefs.fsc.fuelLeverInverted));
        handleSwitchChange(FscSwitchId::TogaLeft, (state.digital & 0x04) == 0);
        handleSwitchChange(FscSwitchId::AutothrottleDisengage, (state.digital & 0x08) == 0);
        handleSwitchChange(FscSwitchId::ParkingBrake, (state.digital & 0x10) == 0);
        handleSwitchChange(FscSwitchId::GearHornCutout, (state.digital & 0x20) == 0);
    }

    if (state.stabTrim >= 0) {
        handleSwitchChange(FscSwitchId::ElTrimGuard, (state.stabTrim & 0x02) != 0);
        handleSwitchChange(FscSwitchId::ApTrimGuard, (state.stabTrim & 0x04) != 0);
    }

    const auto& trimMapping = g_fscProfileRuntime.switches[static_cast<size_t>(FscSwitchId::PitchTrimWheel)];
    if (trimMapping.defined && trimMapping.type == FscSwitchMapping::Type::Encoder && state.trimWheelDelta != 0) {
        bool allowManual = true;
        if (g_prefs.fsc.type == Prefs::FscType::Motorized && state.stabTrim >= 0) {
            bool mainElec = (state.stabTrim & 0x02) != 0;
            bool autoPilot = (state.stabTrim & 0x04) != 0;
            allowManual = mainElec && autoPilot;
        }
        if (allowManual) {
            int steps = std::abs(state.trimWheelDelta);
            const auto& actions = state.trimWheelDelta > 0 ? trimMapping.cwActions : trimMapping.ccwActions;
            for (int i = 0; i < steps; ++i) {
                executeActions(actions);
            }
        }
    }

    auto mapAxis = [&](int raw, const FscAxisMapping& mapping) -> std::optional<float> {
        if (raw < 0) return std::nullopt;
        int minv = 0;
        int maxv = 0;
        if (!getPrefIntByKey(mapping.sourceRefMin, minv) || !getPrefIntByKey(mapping.sourceRefMax, maxv)) {
            return std::nullopt;
        }
        if (maxv == minv) {
            return std::nullopt;
        }
        float f = static_cast<float>(raw - minv) / static_cast<float>(maxv - minv);
        f = clamp01(f);
        bool invert = mapping.invert;
        if (!mapping.invertRef.empty()) {
            bool inv = false;
            if (getPrefBoolByKey(mapping.invertRef, inv)) {
                invert = inv;
            }
        }
        if (invert) {
            f = 1.0f - f;
        }
        return mapping.targetMin + (mapping.targetMax - mapping.targetMin) * f;
    };

    struct ThrottleFilterState {
        int lastRaw1 = -1;
        int lastRaw2 = -1;
        float filtered1 = 0.0f;
        float filtered2 = 0.0f;
        bool init1 = false;
        bool init2 = false;
        std::chrono::steady_clock::time_point lastTime{};
    };
    static ThrottleFilterState throttleFilter;

    auto resetThrottleFilter = [&]() {
        throttleFilter.lastRaw1 = -1;
        throttleFilter.lastRaw2 = -1;
        throttleFilter.init1 = false;
        throttleFilter.init2 = false;
        throttleFilter.lastTime = std::chrono::steady_clock::time_point{};
    };

    const auto& mapT1 = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Throttle1)];
    const auto& mapT2 = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Throttle2)];
    bool motorizedHw = (g_prefs.fsc.type == Prefs::FscType::Motorized);
    bool throttleMotorActive = motorizedHw && g_fscProfileRuntime.motorized.enabled && g_fscMotorThrottleActive.load();
    if (throttleMotorActive) {
        resetThrottleFilter();
    }
    if (!throttleMotorActive) {
        if (!mapT1.defined || state.throttle1 < 0) {
            throttleFilter.lastRaw1 = -1;
        }
        if (!mapT2.defined || state.throttle2 < 0) {
            throttleFilter.lastRaw2 = -1;
        }

        auto applyDeadband = [&](int raw, int& lastRaw) -> int {
            if (raw < 0) {
                return raw;
            }
            if (lastRaw < 0) {
                lastRaw = raw;
                return raw;
            }
            int db = g_prefs.fsc.throttleDeadband;
            if (db > 0 && std::abs(raw - lastRaw) <= db) {
                return lastRaw;
            }
            lastRaw = raw;
            return raw;
        };

        int rawT1 = mapT1.defined ? applyDeadband(state.throttle1, throttleFilter.lastRaw1) : -1;
        int rawT2 = mapT2.defined ? applyDeadband(state.throttle2, throttleFilter.lastRaw2) : -1;

        auto t1 = mapT1.defined ? mapAxis(rawT1, mapT1) : std::nullopt;
        auto t2 = mapT2.defined ? mapAxis(rawT2, mapT2) : std::nullopt;

        float alpha = 1.0f;
        if (g_prefs.fsc.throttleSmoothMs > 0) {
            auto now = std::chrono::steady_clock::now();
            float dt = 0.0f;
            if (throttleFilter.lastTime.time_since_epoch().count() != 0) {
                dt = std::chrono::duration<float>(now - throttleFilter.lastTime).count();
            }
            throttleFilter.lastTime = now;
            if (dt > 0.0f) {
                float tau = static_cast<float>(g_prefs.fsc.throttleSmoothMs) / 1000.0f;
                alpha = dt / (tau + dt);
                if (alpha > 1.0f) {
                    alpha = 1.0f;
                }
            }
        }

        auto smoothValue = [&](std::optional<float> value, float& filtered, bool& init) -> std::optional<float> {
            if (!value.has_value()) {
                init = false;
                return std::nullopt;
            }
            if (!init || alpha >= 0.999f) {
                filtered = *value;
                init = true;
                return filtered;
            }
            filtered = filtered + alpha * (*value - filtered);
            init = true;
            return filtered;
        };

        t1 = smoothValue(t1, throttleFilter.filtered1, throttleFilter.init1);
        t2 = smoothValue(t2, throttleFilter.filtered2, throttleFilter.init2);

        if (t1.has_value() && t2.has_value() && g_prefs.fsc.throttleSyncBand > 0.0f) {
            float diff = std::fabs(*t1 - *t2);
            if (diff <= g_prefs.fsc.throttleSyncBand) {
                float avg = (*t1 + *t2) * 0.5f;
                t1 = avg;
                t2 = avg;
            }
        }

        if (t1.has_value()) {
            for (const auto& target : mapT1.targets) {
                setDatarefValue(target.dataref, target.datarefType, *t1, target.index);
            }
        }
        if (t2.has_value()) {
            for (const auto& target : mapT2.targets) {
                setDatarefValue(target.dataref, target.datarefType, *t2, target.index);
            }
        }
    }

    static float dbgPrevRev1 = -1.0f;
    static float dbgPrevRev2 = -1.0f;

    const auto& mapR1 = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Reverser1)];
    if (mapR1.defined) {
        if (auto r1 = mapAxis(state.reverser1, mapR1)) {
            for (const auto& target : mapR1.targets) {
                setDatarefValue(target.dataref, target.datarefType, *r1, target.index);
            }
            if (g_prefs.fsc.debug) {
                if (dbgPrevRev1 < 0 || std::fabs(*r1 - dbgPrevRev1) > 0.01f) {
                    int minv = 0;
                    int maxv = 0;
                    getPrefIntByKey(mapR1.sourceRefMin, minv);
                    getPrefIntByKey(mapR1.sourceRefMax, maxv);
                    logLine("FSC DBG: rev1 raw=" + std::to_string(state.reverser1) + " mapped=" + std::to_string(*r1) +
                            " min=" + std::to_string(minv) + " max=" + std::to_string(maxv));
                    dbgPrevRev1 = *r1;
                }
            }
        }
    }

    const auto& mapR2 = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Reverser2)];
    if (mapR2.defined) {
        if (auto r2 = mapAxis(state.reverser2, mapR2)) {
            for (const auto& target : mapR2.targets) {
                setDatarefValue(target.dataref, target.datarefType, *r2, target.index);
            }
            if (g_prefs.fsc.debug) {
                if (dbgPrevRev2 < 0 || std::fabs(*r2 - dbgPrevRev2) > 0.01f) {
                    int minv = 0;
                    int maxv = 0;
                    getPrefIntByKey(mapR2.sourceRefMin, minv);
                    getPrefIntByKey(mapR2.sourceRefMax, maxv);
                    logLine("FSC DBG: rev2 raw=" + std::to_string(state.reverser2) + " mapped=" + std::to_string(*r2) +
                            " min=" + std::to_string(minv) + " max=" + std::to_string(maxv));
                    dbgPrevRev2 = *r2;
                }
            }
        }
    }

    if (!g_fscProfileRuntime.speedbrake.enabled) {
        const auto& mapSb = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Speedbrake)];
        if (mapSb.defined) {
            if (auto sb = mapAxis(state.speedbrake, mapSb)) {
                for (const auto& target : mapSb.targets) {
                    setDatarefValue(target.dataref, target.datarefType, *sb, target.index);
                }
            }
        }
    }

    if (!g_fscProfileRuntime.flaps.enabled) {
        const auto& mapFlaps = g_fscProfileRuntime.axes[static_cast<size_t>(FscAxisId::Flaps)];
        if (mapFlaps.defined) {
            if (auto fl = mapAxis(state.flaps, mapFlaps)) {
                for (const auto& target : mapFlaps.targets) {
                    setDatarefValue(target.dataref, target.datarefType, *fl, target.index);
                }
            }
        }
    }

    bool speedbrakeMotorActive = motorizedHw && g_fscProfileRuntime.motorized.enabled && g_fscMotorSpeedbrakeActive.load();
    if (g_fscProfileRuntime.speedbrake.enabled && !speedbrakeMotorActive && state.speedbrake >= 0) {
        int speedbrake = state.speedbrake;
        if (!g_fscProfileRuntime.speedbrake.invertRef.empty()) {
            bool inv = false;
            if (getPrefBoolByKey(g_fscProfileRuntime.speedbrake.invertRef, inv) && inv) {
                speedbrake = 255 - speedbrake;
            }
        }

        auto readRatio = [&]() -> std::optional<float> {
            if (!g_fscProfileRuntime.speedbrake.ratioDataref) {
                return std::nullopt;
            }
            float value = 0.0f;
            if (!readDatarefValue(g_fscProfileRuntime.speedbrake.ratioDataref,
                                  g_fscProfileRuntime.speedbrake.ratioDatarefType, value)) {
                return std::nullopt;
            }
            return value;
        };

        auto approxEqual = [](float a, float b) {
            return std::fabs(a - b) <= 0.001f;
        };

        auto handleDetent = [&](const FscSpeedbrakeDetent& detent, int stateCode) {
            int ref = 0;
            if (!getPrefIntByKey(detent.sourceRef, ref)) {
                return;
            }
            if (std::abs(speedbrake - ref) > detent.tolerance) {
                return;
            }
            if (g_fscPrev.speedbrakeState == stateCode) {
                return;
            }
            if (detent.hasConditional) {
                auto ratio = readRatio();
                if (ratio.has_value() && *ratio == 0.0f) {
                    executeActions(detent.actionsIfRatioZero);
                } else {
                    executeActions(detent.actionsIfRatioNonzero);
                }
            } else {
                executeActions(detent.actions);
            }
            if (detent.hasConfirm) {
                auto ratio = readRatio();
                if (ratio.has_value() && approxEqual(*ratio, detent.confirmValue)) {
                    g_fscPrev.speedbrakeState = stateCode;
                }
            } else {
                g_fscPrev.speedbrakeState = stateCode;
            }
        };

        handleDetent(g_fscProfileRuntime.speedbrake.detentDown, 0);
        handleDetent(g_fscProfileRuntime.speedbrake.detentArmed, 1);
        handleDetent(g_fscProfileRuntime.speedbrake.detentUp, 4);

        int minRef = 0;
        int maxRef = 0;
        if (getPrefIntByKey(g_fscProfileRuntime.speedbrake.sourceRefMin, minRef) &&
            getPrefIntByKey(g_fscProfileRuntime.speedbrake.sourceRefMax, maxRef) &&
            speedbrake >= minRef && speedbrake <= maxRef) {
            if (speedbrake != g_fscPrev.speedbrakePrev) {
                int range = maxRef - minRef;
                if (range != 0) {
                    float ratio = static_cast<float>(speedbrake - minRef) / static_cast<float>(range);
                    ratio = clamp01(ratio);
                    for (const auto& target : g_fscProfileRuntime.speedbrake.analogTargets) {
                        float out = target.targetMin + (target.targetMax - target.targetMin) * ratio;
                        setDatarefValue(target.dataref, target.datarefType, out, target.index);
                    }
                    g_fscPrev.speedbrakeState = 2;
                }
            }
            g_fscPrev.speedbrakePrev = speedbrake;
        } else if (getPrefIntByKey(g_fscProfileRuntime.speedbrake.detentUp.sourceRef, maxRef) &&
                   speedbrake < maxRef &&
                   getPrefIntByKey(g_fscProfileRuntime.speedbrake.sourceRefMax, minRef) &&
                   speedbrake > minRef &&
                   g_fscPrev.speedbrakeState != 3) {
            g_fscPrev.speedbrakeState = 3;
        }
    }

    if (g_fscProfileRuntime.flaps.enabled && state.flaps >= 0) {
        int bestIdx = -1;
        int bestDiff = 9999;
        int bestValue = -1;
        for (size_t i = 0; i < g_fscProfileRuntime.flaps.positions.size(); ++i) {
            const auto& pos = g_fscProfileRuntime.flaps.positions[i];
            int ref = 0;
            if (!getPrefIntByKey(pos.sourceRef, ref)) {
                continue;
            }
            int diff = std::abs(state.flaps - ref);
            if (g_fscProfileRuntime.flaps.modeNearest) {
                if (diff < bestDiff) {
                    bestDiff = diff;
                    bestIdx = static_cast<int>(i);
                    bestValue = ref;
                }
            } else {
                if (diff <= pos.tolerance) {
                    bestIdx = static_cast<int>(i);
                    bestValue = ref;
                    break;
                }
            }
        }
        if (bestIdx >= 0) {
            const auto& pos = g_fscProfileRuntime.flaps.positions[static_cast<size_t>(bestIdx)];
            if (!(g_fscProfileRuntime.flaps.modeNearest && bestDiff > pos.tolerance)) {
                if (g_fscPrev.flaps != bestValue) {
                    executeActions(pos.actions);
                    g_fscPrev.flaps = bestValue;
                }
            }
        }
    }
}

std::string fscCalibPrompt(FscCalibStep step, Prefs::FscType type) {
    switch (step) {
        case FscCalibStep::SpeedbrakeDown:
            return "Set speedbrake fully DOWN, then NEXT.";
        case FscCalibStep::SpeedbrakeArmed:
            return "Set speedbrake to ARMED, then NEXT.";
        case FscCalibStep::SpeedbrakeMin:
            return "Set speedbrake just above ARMED (start of travel), then NEXT.";
        case FscCalibStep::SpeedbrakeDetent:
            return "Set speedbrake to FLIGHT DETENT, then NEXT.";
        case FscCalibStep::SpeedbrakeUp:
            return "Set speedbrake fully UP, then NEXT.";
        case FscCalibStep::Throttle1Min:
            return "Set Throttle 1 to IDLE/MIN, then NEXT.";
        case FscCalibStep::Throttle1Full:
            return "Set Throttle 1 to FULL, then NEXT.";
        case FscCalibStep::Throttle2Min:
            return "Set Throttle 2 to IDLE/MIN, then NEXT.";
        case FscCalibStep::Throttle2Full:
            return "Set Throttle 2 to FULL, then NEXT.";
        case FscCalibStep::Reverser1Min:
            return "Set Reverser 1 to STOWED (MIN), then NEXT.";
        case FscCalibStep::Reverser1Max:
            return "Set Reverser 1 to FULL REVERSE (MAX), then NEXT.";
        case FscCalibStep::Reverser2Min:
            return "Set Reverser 2 to STOWED (MIN), then NEXT.";
        case FscCalibStep::Reverser2Max:
            return "Set Reverser 2 to FULL REVERSE (MAX), then NEXT.";
        case FscCalibStep::Flaps00:
            return "Set flaps to 0, then NEXT.";
        case FscCalibStep::Flaps01:
            return "Set flaps to 1, then NEXT.";
        case FscCalibStep::Flaps02:
            return "Set flaps to 2, then NEXT.";
        case FscCalibStep::Flaps05:
            return "Set flaps to 5, then NEXT.";
        case FscCalibStep::Flaps10:
            return "Set flaps to 10, then NEXT.";
        case FscCalibStep::Flaps15:
            return "Set flaps to 15, then NEXT.";
        case FscCalibStep::Flaps25:
            return "Set flaps to 25, then NEXT.";
        case FscCalibStep::Flaps30:
            return "Set flaps to 30, then NEXT.";
        case FscCalibStep::Flaps40:
            return "Set flaps to 40, then NEXT.";
    }
    if (type == Prefs::FscType::SemiPro) {
        return "Unknown calibration step (SEMIPRO).";
    }
    return "Unknown calibration step.";
}

void fscCalibAnnounce(const std::string& msg) {
    logLine("FSC CAL: " + msg);
    setFscCalibStatus(msg);
    std::string speak = "FSC calibration. " + msg;
    XPLMSpeakString(speak.c_str());
}

bool captureCalibValue(const std::string& label,
                       const std::optional<IntRingBuffer<64>::Stats>& stats,
                       int stableRange,
                       int minSamples,
                       int& outValue) {
    if (!stats.has_value() || static_cast<int>(stats->count) < minSamples) {
        fscCalibAnnounce("No stable values for " + label + " yet (hold steady briefly, then NEXT).");
        return false;
    }
    int range = stats->max - stats->min;
    if (range > stableRange) {
        fscCalibAnnounce(label + " is unstable (range=" + std::to_string(range) + ", median=" + std::to_string(stats->median) +
                         "). Hold steady and press NEXT again.");
        return false;
    }
    outValue = stats->median;
    logLine("FSC CAL: captured " + label + "=" + std::to_string(outValue) + " (range=" + std::to_string(range) + ")");
    return true;
}

void finishFscCalibrationLocked() {
    // Determine speedbrake direction and normalize captured values.
    bool reversed = (g_fscCalib.sbDownRaw >= 0 && g_fscCalib.sbUpRaw >= 0 && g_fscCalib.sbUpRaw < g_fscCalib.sbDownRaw);
    auto normSb = [reversed](int raw) -> int {
        if (raw < 0) return raw;
        return reversed ? (255 - raw) : raw;
    };

    int sbDown = normSb(g_fscCalib.sbDownRaw);
    int sbArmed = normSb(g_fscCalib.sbArmedRaw);
    int sbMin = normSb(g_fscCalib.sbMinRaw);
    int sbDetent = normSb(g_fscCalib.sbDetentRaw);
    int sbUp = normSb(g_fscCalib.sbUpRaw);

    g_prefs.fsc.speedBrakeReversed = reversed;
    if (sbDown >= 0) g_prefs.fsc.calib.spoilersDown = sbDown;
    if (sbArmed >= 0) g_prefs.fsc.calib.spoilersArmed = sbArmed;
    if (sbMin >= 0) g_prefs.fsc.calib.spoilersMin = sbMin;
    if (sbDetent >= 0) g_prefs.fsc.calib.spoilersDetent = sbDetent;
    if (sbUp >= 0) g_prefs.fsc.calib.spoilersUp = sbUp;

    if (g_fscCalib.throttle1Min >= 0) g_prefs.fsc.calib.throttle1Min = g_fscCalib.throttle1Min;
    if (g_fscCalib.throttle1Full >= 0) g_prefs.fsc.calib.throttle1Full = g_fscCalib.throttle1Full;
    if (g_fscCalib.throttle2Min >= 0) g_prefs.fsc.calib.throttle2Min = g_fscCalib.throttle2Min;
    if (g_fscCalib.throttle2Full >= 0) g_prefs.fsc.calib.throttle2Full = g_fscCalib.throttle2Full;
    if (g_fscCalib.reverser1Min >= 0) g_prefs.fsc.calib.reverser1Min = g_fscCalib.reverser1Min;
    if (g_fscCalib.reverser1Max >= 0) g_prefs.fsc.calib.reverser1Max = g_fscCalib.reverser1Max;
    if (g_fscCalib.reverser2Min >= 0) g_prefs.fsc.calib.reverser2Min = g_fscCalib.reverser2Min;
    if (g_fscCalib.reverser2Max >= 0) g_prefs.fsc.calib.reverser2Max = g_fscCalib.reverser2Max;

    if (g_fscCalib.type == Prefs::FscType::SemiPro) {
        if (g_fscCalib.flaps00 >= 0) g_prefs.fsc.calib.flaps00 = g_fscCalib.flaps00;
        if (g_fscCalib.flaps01 >= 0) g_prefs.fsc.calib.flaps01 = g_fscCalib.flaps01;
        if (g_fscCalib.flaps02 >= 0) g_prefs.fsc.calib.flaps02 = g_fscCalib.flaps02;
        if (g_fscCalib.flaps05 >= 0) g_prefs.fsc.calib.flaps05 = g_fscCalib.flaps05;
        if (g_fscCalib.flaps10 >= 0) g_prefs.fsc.calib.flaps10 = g_fscCalib.flaps10;
        if (g_fscCalib.flaps15 >= 0) g_prefs.fsc.calib.flaps15 = g_fscCalib.flaps15;
        if (g_fscCalib.flaps25 >= 0) g_prefs.fsc.calib.flaps25 = g_fscCalib.flaps25;
        if (g_fscCalib.flaps30 >= 0) g_prefs.fsc.calib.flaps30 = g_fscCalib.flaps30;
        if (g_fscCalib.flaps40 >= 0) g_prefs.fsc.calib.flaps40 = g_fscCalib.flaps40;
    }

    logLine("FSC CAL: ==== Copy into " + std::string(PLUGIN_PREFS_FILE) + " ====");
    logLine("fsc.type=" + fscTypeToString(g_fscCalib.type));
    logLine("fsc.speed_brake_reversed=" + std::string(reversed ? "1" : "0"));
    logLine("fsc.calib.spoilers_down=" + std::to_string(sbDown));
    logLine("fsc.calib.spoilers_armed=" + std::to_string(sbArmed));
    logLine("fsc.calib.spoilers_min=" + std::to_string(sbMin));
    logLine("fsc.calib.spoilers_detent=" + std::to_string(sbDetent));
    logLine("fsc.calib.spoilers_up=" + std::to_string(sbUp));
    logLine("fsc.calib.throttle1_min=" + std::to_string(g_fscCalib.throttle1Min));
    logLine("fsc.calib.throttle1_full=" + std::to_string(g_fscCalib.throttle1Full));
    logLine("fsc.calib.throttle2_min=" + std::to_string(g_fscCalib.throttle2Min));
    logLine("fsc.calib.throttle2_full=" + std::to_string(g_fscCalib.throttle2Full));
    logLine("fsc.calib.reverser1_min=" + std::to_string(g_fscCalib.reverser1Min));
    logLine("fsc.calib.reverser1_max=" + std::to_string(g_fscCalib.reverser1Max));
    logLine("fsc.calib.reverser2_min=" + std::to_string(g_fscCalib.reverser2Min));
    logLine("fsc.calib.reverser2_max=" + std::to_string(g_fscCalib.reverser2Max));
    if (g_fscCalib.type == Prefs::FscType::SemiPro) {
        logLine("fsc.calib.flaps_00=" + std::to_string(g_fscCalib.flaps00));
        logLine("fsc.calib.flaps_01=" + std::to_string(g_fscCalib.flaps01));
        logLine("fsc.calib.flaps_02=" + std::to_string(g_fscCalib.flaps02));
        logLine("fsc.calib.flaps_05=" + std::to_string(g_fscCalib.flaps05));
        logLine("fsc.calib.flaps_10=" + std::to_string(g_fscCalib.flaps10));
        logLine("fsc.calib.flaps_15=" + std::to_string(g_fscCalib.flaps15));
        logLine("fsc.calib.flaps_25=" + std::to_string(g_fscCalib.flaps25));
        logLine("fsc.calib.flaps_30=" + std::to_string(g_fscCalib.flaps30));
        logLine("fsc.calib.flaps_40=" + std::to_string(g_fscCalib.flaps40));
    }
    if (!writeFscCalibToPrefsFile(g_prefs)) {
        logLine("FSC CAL: failed to persist calibration to prefs (see above).");
    }
    logLine("FSC CAL: ===============================");
}

void startFscCalibrationWizard() {
    if (!g_prefs.fsc.enabled) {
        fscCalibAnnounce("FSC is disabled. Set fsc.enabled=1 and reload the plugin.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_fscCalibMutex);
        if (g_fscCalib.active) {
            fscCalibAnnounce("Calibration already running. Use NEXT or CANCEL.");
            return;
        }
        g_fscCalib.reset(g_prefs.fsc.type);
    }
    fscCalibAnnounce("Calibration started. Axis outputs to Zibo are suspended.");
    fscCalibAnnounce(fscCalibPrompt(g_fscCalib.steps[0], g_fscCalib.type));
}

void cancelFscCalibrationWizard() {
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(g_fscCalibMutex);
        wasActive = g_fscCalib.active;
        g_fscCalib.active = false;
    }
    if (wasActive) {
        fscCalibAnnounce("Calibration canceled.");
    } else {
        fscCalibAnnounce("No calibration active.");
    }
}

void nextFscCalibrationWizard() {
    constexpr int kStableRange = 3;
    constexpr int kMinSamples = 10;

    std::string nextPrompt;
    {
        std::lock_guard<std::mutex> lock(g_fscCalibMutex);
        if (!g_fscCalib.active) {
            // Defer announce outside the lock.
        } else if (g_fscCalib.stepIndex >= g_fscCalib.steps.size()) {
            // Should not happen; treat as finish.
            finishFscCalibrationLocked();
            g_fscCalib.active = false;
            nextPrompt = "Calibration complete. Prefs updated; check the log.";
        } else {
            const auto step = g_fscCalib.steps[g_fscCalib.stepIndex];
            std::optional<IntRingBuffer<64>::Stats> stats;
            std::string label;
            switch (step) {
                case FscCalibStep::SpeedbrakeDown:
                case FscCalibStep::SpeedbrakeArmed:
                case FscCalibStep::SpeedbrakeMin:
                case FscCalibStep::SpeedbrakeDetent:
                case FscCalibStep::SpeedbrakeUp:
                    stats = g_fscCalib.histSpeedbrake.stats();
                    label = "speedbrake";
                    break;
                case FscCalibStep::Throttle1Min:
                case FscCalibStep::Throttle1Full:
                    stats = g_fscCalib.histThrottle1.stats();
                    label = "throttle1";
                    break;
                case FscCalibStep::Throttle2Min:
                case FscCalibStep::Throttle2Full:
                    stats = g_fscCalib.histThrottle2.stats();
                    label = "throttle2";
                    break;
                case FscCalibStep::Reverser1Min:
                case FscCalibStep::Reverser1Max:
                    stats = g_fscCalib.histReverser1.stats();
                    label = "reverser1";
                    break;
                case FscCalibStep::Reverser2Min:
                case FscCalibStep::Reverser2Max:
                    stats = g_fscCalib.histReverser2.stats();
                    label = "reverser2";
                    break;
                case FscCalibStep::Flaps00:
                case FscCalibStep::Flaps01:
                case FscCalibStep::Flaps02:
                case FscCalibStep::Flaps05:
                case FscCalibStep::Flaps10:
                case FscCalibStep::Flaps15:
                case FscCalibStep::Flaps25:
                case FscCalibStep::Flaps30:
                case FscCalibStep::Flaps40:
                    stats = g_fscCalib.histFlaps.stats();
                    label = "flaps";
                    break;
            }

            int value = -1;
            if (!captureCalibValue(label, stats, kStableRange, kMinSamples, value)) {
                nextPrompt = fscCalibPrompt(step, g_fscCalib.type);
            } else {
                switch (step) {
                    case FscCalibStep::SpeedbrakeDown: g_fscCalib.sbDownRaw = value; break;
                    case FscCalibStep::SpeedbrakeArmed: g_fscCalib.sbArmedRaw = value; break;
                    case FscCalibStep::SpeedbrakeMin: g_fscCalib.sbMinRaw = value; break;
                    case FscCalibStep::SpeedbrakeDetent: g_fscCalib.sbDetentRaw = value; break;
                    case FscCalibStep::SpeedbrakeUp: g_fscCalib.sbUpRaw = value; break;
                    case FscCalibStep::Throttle1Min: g_fscCalib.throttle1Min = value; break;
                    case FscCalibStep::Throttle1Full: g_fscCalib.throttle1Full = value; break;
                    case FscCalibStep::Throttle2Min: g_fscCalib.throttle2Min = value; break;
                    case FscCalibStep::Throttle2Full: g_fscCalib.throttle2Full = value; break;
                    case FscCalibStep::Reverser1Min: g_fscCalib.reverser1Min = value; break;
                    case FscCalibStep::Reverser1Max: g_fscCalib.reverser1Max = value; break;
                    case FscCalibStep::Reverser2Min: g_fscCalib.reverser2Min = value; break;
                    case FscCalibStep::Reverser2Max: g_fscCalib.reverser2Max = value; break;
                    case FscCalibStep::Flaps00: g_fscCalib.flaps00 = value; break;
                    case FscCalibStep::Flaps01: g_fscCalib.flaps01 = value; break;
                    case FscCalibStep::Flaps02: g_fscCalib.flaps02 = value; break;
                    case FscCalibStep::Flaps05: g_fscCalib.flaps05 = value; break;
                    case FscCalibStep::Flaps10: g_fscCalib.flaps10 = value; break;
                    case FscCalibStep::Flaps15: g_fscCalib.flaps15 = value; break;
                    case FscCalibStep::Flaps25: g_fscCalib.flaps25 = value; break;
                    case FscCalibStep::Flaps30: g_fscCalib.flaps30 = value; break;
                    case FscCalibStep::Flaps40: g_fscCalib.flaps40 = value; break;
                }
                ++g_fscCalib.stepIndex;
                if (g_fscCalib.stepIndex >= g_fscCalib.steps.size()) {
                    finishFscCalibrationLocked();
                    g_fscCalib.active = false;
                    nextPrompt = "Calibration complete. Prefs updated; check the log.";
                } else {
                    nextPrompt = fscCalibPrompt(g_fscCalib.steps[g_fscCalib.stepIndex], g_fscCalib.type);
                }
            }
        }
    }

    if (nextPrompt.empty()) {
        fscCalibAnnounce("No calibration active. Run START.");
    } else {
        fscCalibAnnounce(nextPrompt);
    }
}

int fscCalibCommandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase != xplm_CommandBegin) {
        return 1;
    }
    if (cmd == g_cmdFscCalibStart) {
        startFscCalibrationWizard();
    } else if (cmd == g_cmdFscCalibNext) {
        nextFscCalibrationWizard();
    } else if (cmd == g_cmdFscCalibCancel) {
        cancelFscCalibrationWizard();
    }
    return 1;
}

void updateFscCalibration(const FscState& inputState) {
    std::lock_guard<std::mutex> lock(g_fscCalibMutex);
    if (!g_fscCalib.active) {
        return;
    }
    if (!g_fscCalib.safeOutputsSent && g_fscCalib.type == Prefs::FscType::Motorized && fscIsConnected()) {
        fscWriteFrame(0x93, 0x00, 0x00);  // motors off
        g_fscMotorThrottleActive.store(false);
        g_fscMotorSpeedbrakeActive.store(false);
        g_fscCalib.safeOutputsSent = true;
    }
    if (inputState.speedbrake >= 0) g_fscCalib.histSpeedbrake.push(inputState.speedbrake);
    if (inputState.throttle1 >= 0) g_fscCalib.histThrottle1.push(inputState.throttle1);
    if (inputState.throttle2 >= 0) g_fscCalib.histThrottle2.push(inputState.throttle2);
    if (inputState.reverser1 >= 0) g_fscCalib.histReverser1.push(inputState.reverser1);
    if (inputState.reverser2 >= 0) g_fscCalib.histReverser2.push(inputState.reverser2);
    if (g_fscCalib.type == Prefs::FscType::SemiPro && inputState.flaps >= 0) g_fscCalib.histFlaps.push(inputState.flaps);
}

bool fscIsConnected() {
    return g_fscFd.load() >= 0;
}

void processFscOutputs(const FscState& inputState) {
    if (!fscIsConnected() || !g_fscProfileActive.load()) {
        return;
    }

    bool parkLightAvailable = false;
    bool parkLightOn = false;
    {
        const auto& mapping = g_fscProfileRuntime.indicators[static_cast<size_t>(FscIndicatorId::ParkingBrakeLight)];
        if (mapping.defined && mapping.dataref) {
            float value = 0.0f;
            if (readDatarefValue(mapping.dataref, mapping.datarefType, value)) {
                parkLightOn = value >= mapping.onMin;
                if (mapping.invert) {
                    parkLightOn = !parkLightOn;
                }
                parkLightAvailable = true;
            }
        }
    }
    bool parkLightChanged = parkLightAvailable &&
        (!g_fscOut.parkBrakeLightKnown || parkLightOn != g_fscOut.parkBrakeLightOn);

    bool motorizedHw = (g_prefs.fsc.type == Prefs::FscType::Motorized);
    if (!motorizedHw || !g_fscProfileRuntime.motorized.enabled) {
        if (parkLightChanged) {
            if (parkLightOn) {
                fscWriteFrame(0x87, 0x11, 0x00);
            } else {
                fscWriteFrame(0x87, 0x10, 0x00);
            }
            g_fscOut.parkBrakeLightKnown = true;
            g_fscOut.parkBrakeLightOn = parkLightOn;
        }
        return;
    }

    const auto& motor = g_fscProfileRuntime.motorized;
    auto now = std::chrono::steady_clock::now();

    // Decide if we are currently moving motorized speedbrake/trim indicator.
    bool speedbrakeMotorActive = now < g_fscOut.speedbrakeMotorOffTime;
    bool trimIndMotorActive = now < g_fscOut.trimIndMotorOffTime;
    g_fscMotorSpeedbrakeActive.store(speedbrakeMotorActive);

    // Digital state mask (solenoids/backlight/trim motor direction)
    uint8_t digitalMask = 0;
    bool wroteDigital = false;
    bool onGround = false;
    if (motor.onGroundDataref) {
        float value = 0.0f;
        if (readDatarefValue(motor.onGroundDataref, motor.onGroundDatarefType, value)) {
            onGround = value >= 0.5f;
        }
    }
    if (onGround || speedbrakeMotorActive) {
        digitalMask |= 0x04;  // speed brake solenoid
    }

    // Park brake solenoid: both toe brakes pressed while parking brake set
    bool parkSolenoid = false;
    {
        const auto& parkSwitch = g_fscProfileRuntime.switches[static_cast<size_t>(FscSwitchId::ParkingBrake)];
        if (parkSwitch.stateDataref && motor.leftToeBrakeDataref && motor.rightToeBrakeDataref) {
            float parkPos = 0.0f;
            float leftToe = 0.0f;
            float rightToe = 0.0f;
            if (readDatarefValue(parkSwitch.stateDataref, parkSwitch.stateDatarefType, parkPos) &&
                readDatarefValue(motor.leftToeBrakeDataref, motor.leftToeBrakeDatarefType, leftToe) &&
                readDatarefValue(motor.rightToeBrakeDataref, motor.rightToeBrakeDatarefType, rightToe)) {
                if (parkPos >= 0.5f) {
                    parkSolenoid = (leftToe >= 0.175f) && (rightToe >= 0.175f);
                }
            }
        }
    }
    if (parkSolenoid) {
        digitalMask |= 0x08;
    }

    // Backlight follows battery bus status.
    {
        const auto& backlight = g_fscProfileRuntime.indicators[static_cast<size_t>(FscIndicatorId::Backlight)];
        if (backlight.defined && backlight.dataref) {
            float value = 0.0f;
            if (readDatarefValue(backlight.dataref, backlight.datarefType, value)) {
                bool on = value >= backlight.onMin;
                if (backlight.invert) {
                    on = !on;
                }
                if (on) {
                    digitalMask |= 0x10;
                }
            }
        }
    }

    // Motorized trim wheel direction (drive wheel when not in manual mode).
    bool manualTrimMode = false;
    if (inputState.stabTrim >= 0) {
        bool mainElec = (inputState.stabTrim & 0x02) != 0;
        bool autoPilot = (inputState.stabTrim & 0x04) != 0;
        manualTrimMode = mainElec && autoPilot;
    }
    if (!manualTrimMode && motor.trimIndicator.wheelDataref) {
        float trimWheel = 0.0f;
        if (!readDatarefValue(motor.trimIndicator.wheelDataref, motor.trimIndicator.wheelDatarefType, trimWheel)) {
            trimWheel = 0.0f;
        }
        if (std::isfinite(g_fscOut.lastTrimWheel)) {
            if (trimWheel > g_fscOut.lastTrimWheel + 0.0001f) {
                digitalMask |= 0x20;  // trim wheel motor UP
            } else if (trimWheel < g_fscOut.lastTrimWheel - 0.0001f) {
                digitalMask |= 0x40;  // trim wheel motor DOWN
            }
        }
        g_fscOut.lastTrimWheel = trimWheel;
    }

    if (digitalMask != g_fscOut.digitalMask) {
        fscWriteFrame(0x87, 0x10, digitalMask);
        g_fscOut.digitalMask = digitalMask;
        wroteDigital = true;
    }
    if (parkLightChanged && !parkLightOn) {
        // Some firmwares use 0x87 0x10 0x00 as park-brake-light OFF; reapply digital mask afterwards.
        fscWriteFrame(0x87, 0x10, 0x00);
        fscWriteFrame(0x87, 0x10, digitalMask);
        wroteDigital = true;
    }
    if (parkLightOn && (parkLightChanged || wroteDigital)) {
        // Restore desired park-brake-light state after any 0x87 0x10 write.
        fscWriteFrame(0x87, 0x11, 0x00);
    }
    if (parkLightChanged) {
        g_fscOut.parkBrakeLightKnown = true;
        g_fscOut.parkBrakeLightOn = parkLightOn;
    }

    // Motorized throttle motors (follow Zibo thrust lever when autothrottle locks throttles).
    bool throttleMotors = false;
    int motorThr1 = g_fscOut.motorThrottle1Pos;
    int motorThr2 = g_fscOut.motorThrottle2Pos;
    if (motor.throttleFollow.lockDataref && motor.throttleFollow.armDataref && motor.throttleFollow.leverDataref) {
        float lockVal = 0.0f;
        float armVal = 0.0f;
        float lever = 0.0f;
        if (readDatarefValue(motor.throttleFollow.lockDataref, motor.throttleFollow.lockDatarefType, lockVal) &&
            readDatarefValue(motor.throttleFollow.armDataref, motor.throttleFollow.armDatarefType, armVal) &&
            readDatarefValue(motor.throttleFollow.leverDataref, motor.throttleFollow.leverDatarefType, lever)) {
            throttleMotors = (lockVal >= 0.5f) && (armVal >= 0.5f);
            if (throttleMotors) {
                int t1Min = 0;
                int t1Max = 0;
                int t2Min = 0;
                int t2Max = 0;
                if (getPrefIntByKey(motor.throttleFollow.thr1MinRef, t1Min) &&
                    getPrefIntByKey(motor.throttleFollow.thr1MaxRef, t1Max) &&
                    getPrefIntByKey(motor.throttleFollow.thr2MinRef, t2Min) &&
                    getPrefIntByKey(motor.throttleFollow.thr2MaxRef, t2Max)) {
                    float norm = clamp01(lever);
                    motorThr1 = static_cast<int>(std::lround(t1Min + (t1Max - t1Min) * norm));
                    motorThr2 = static_cast<int>(std::lround(t2Min + (t2Max - t2Min) * norm));
                }
            }
        }
    }
    g_fscMotorThrottleActive.store(throttleMotors);

    if (throttleMotors) {
        float updateRate = 0.0f;
        if (!getPrefFloatByKey(motor.throttleFollow.updateRateRef, updateRate)) {
            updateRate = 0.07f;
        }
        if (g_fscOut.lastThrottleUpdate.time_since_epoch().count() == 0 ||
            now - g_fscOut.lastThrottleUpdate > std::chrono::duration<float>(updateRate)) {
            if (motorThr1 != g_fscOut.motorThrottle1Pos) {
                fscWritePosition(0x00, motorThr1);
                g_fscOut.motorThrottle1Pos = motorThr1;
            }
            if (motorThr2 != g_fscOut.motorThrottle2Pos) {
                fscWritePosition(0x10, motorThr2);
                g_fscOut.motorThrottle2Pos = motorThr2;
            }
            g_fscOut.lastThrottleUpdate = now;
        }
    }

    // Motorized speedbrake auto deploy/stow (moves physical lever).
    if (!speedbrakeMotorActive && motor.speedbrake.ratioDataref && inputState.speedbrake >= 0) {
        float ratio = 0.0f;
        if (!readDatarefValue(motor.speedbrake.ratioDataref, motor.speedbrake.ratioDatarefType, ratio)) {
            ratio = 0.0f;
        }
        int sb = inputState.speedbrake;
        if (g_prefs.fsc.speedBrakeReversed) {
            sb = 255 - sb;
        }
        int armRef = 0;
        int upRef = 0;
        int motorDown = 0;
        int motorUp = 0;
        if (getPrefIntByKey(motor.speedbrake.armRef, armRef) &&
            getPrefIntByKey(motor.speedbrake.upRef, upRef) &&
            getPrefIntByKey(motor.speedbrake.motorDownRef, motorDown) &&
            getPrefIntByKey(motor.speedbrake.motorUpRef, motorUp)) {
            if (ratio >= motor.speedbrake.ratioUpMin && std::abs(sb - armRef) <= motor.speedbrake.tolerance) {
                g_fscOut.motorSpeedbrakePos = motorUp;
                fscWritePosition(0x20, g_fscOut.motorSpeedbrakePos);
                g_fscOut.speedbrakeMotorOffTime = now + std::chrono::milliseconds(motor.speedbrake.holdMs);
                speedbrakeMotorActive = true;
                digitalMask |= 0x04;
            } else if (ratio <= motor.speedbrake.ratioDownMax && std::abs(sb - upRef) <= motor.speedbrake.tolerance) {
                g_fscOut.motorSpeedbrakePos = motorDown;
                fscWritePosition(0x20, g_fscOut.motorSpeedbrakePos);
                g_fscOut.speedbrakeMotorOffTime = now + std::chrono::milliseconds(motor.speedbrake.holdMs);
                speedbrakeMotorActive = true;
                digitalMask |= 0x04;
            }
        }
        if (speedbrakeMotorActive && digitalMask != g_fscOut.digitalMask) {
            fscWriteFrame(0x87, 0x10, digitalMask);
            g_fscOut.digitalMask = digitalMask;
            if (parkLightOn) {
                fscWriteFrame(0x87, 0x11, 0x00);
            }
        }
    }
    g_fscMotorSpeedbrakeActive.store(speedbrakeMotorActive);

    // Motorized trim indicator (move arrow to match trim wheel position).
    if (motor.trimIndicator.wheelDataref) {
        float wheelMin = 0.0f;
        float wheelMax = 0.0f;
        int arrowMin = 0;
        int arrowMax = 0;
        if (getPrefFloatByKey(motor.trimIndicator.wheelMinRef, wheelMin) &&
            getPrefFloatByKey(motor.trimIndicator.wheelMaxRef, wheelMax) &&
            getPrefIntByKey(motor.trimIndicator.arrowMinRef, arrowMin) &&
            getPrefIntByKey(motor.trimIndicator.arrowMaxRef, arrowMax) &&
            std::isfinite(wheelMin) && std::isfinite(wheelMax) && wheelMax != wheelMin) {
            float tw = 0.0f;
            if (readDatarefValue(motor.trimIndicator.wheelDataref, motor.trimIndicator.wheelDatarefType, tw)) {
                float norm = (tw - wheelMin) / (wheelMax - wheelMin);
                norm = clamp01(norm);
                int target = static_cast<int>(std::lround(arrowMin + (arrowMax - arrowMin) * norm));
                target = std::clamp(target, std::min(arrowMin, arrowMax), std::max(arrowMin, arrowMax));
                if (target != g_fscOut.motorTrimIndPos) {
                    g_fscOut.motorTrimIndPos = target;
                    fscWritePosition(0x30, target);
                    g_fscOut.trimIndMotorOffTime = now + std::chrono::milliseconds(motor.trimIndicator.holdMs);
                    trimIndMotorActive = true;
                }
            }
        }
    }
    trimIndMotorActive = now < g_fscOut.trimIndMotorOffTime;

    // Motor power mask
    uint8_t motorPower = 0;
    if (throttleMotors) motorPower |= 0x01 | 0x02;
    if (speedbrakeMotorActive) motorPower |= 0x04;
    if (trimIndMotorActive) motorPower |= 0x08;
    if (motorPower != g_fscOut.motorPowerMask) {
        fscWriteFrame(0x93, 0x00, motorPower);
        g_fscOut.motorPowerMask = motorPower;
    }
}

void fscLoop() {
    auto lastPoll = std::chrono::steady_clock::now();
    auto lastRx = std::chrono::steady_clock::now();
    auto lastDiag = std::chrono::steady_clock::now();
    uint64_t packets = 0;
    uint64_t badReads = 0;
    std::array<bool, 128> unknownLogged{};
    bool rawCaptureActive = false;
    auto rawCaptureUntil = std::chrono::steady_clock::time_point{};
    auto rawLastFlush = std::chrono::steady_clock::time_point{};
    uint64_t rawBytes = 0;
    int rawLineBytes = 0;
    std::string rawLine;

    auto startRawCapture = [&]() {
        if (!g_prefs.fsc.debug) {
            return;
        }
        rawCaptureActive = true;
        rawCaptureUntil = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        rawLastFlush = std::chrono::steady_clock::now();
        rawBytes = 0;
        rawLineBytes = 0;
        rawLine.clear();
        logLine("FSC RAW: capture start (10s/2048 bytes max)");
    };

    auto flushRaw = [&](std::chrono::steady_clock::time_point now, bool force) {
        if (!rawCaptureActive) {
            return;
        }
        if (!rawLine.empty() && (force || rawLineBytes >= 16 || now - rawLastFlush > std::chrono::seconds(1))) {
            logLine("FSC RAW: " + rawLine);
            rawLine.clear();
            rawLineBytes = 0;
            rawLastFlush = now;
        }
        if (force) {
            logLine("FSC RAW: capture end (bytes=" + std::to_string(rawBytes) + ")");
            rawCaptureActive = false;
        }
    };

    while (g_fscRunning.load()) {
        intptr_t currentHandle = g_fscFd.load();
        if (currentHandle < 0) {
            intptr_t openedHandle = openFscPort(g_prefs.fsc.port, g_prefs.fsc.serial);
            if (openedHandle < 0) {
#if IBM
                DWORD e = GetLastError();
                logLine("FSC: failed to open port " + g_prefs.fsc.port + " (" + std::to_string(e) + "): " +
                        win32ErrorMessage(e));
#else
                int e = errno;
                logLine("FSC: failed to open port " + g_prefs.fsc.port + " (" + std::to_string(e) + "): " +
                        std::strerror(e));
#endif
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(g_fscIoMutex);
                g_fscFd.store(openedHandle);
            }
            logLine("FSC: opened " + g_prefs.fsc.port);
            logLine("FSC: serial " + fscSerialSummary(g_prefs.fsc.serial));
            startRawCapture();
            fscSendPoll();
            lastPoll = std::chrono::steady_clock::now();
            lastRx = lastPoll;
        }

        uint8_t b1 = 0;
        currentHandle = g_fscFd.load();
        if (currentHandle < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        int r1 = readByteWithTimeout(currentHandle, b1, 500);
        if (r1 == 0) {
            auto now = std::chrono::steady_clock::now();
            if (rawCaptureActive && now > rawCaptureUntil) {
                flushRaw(now, true);
            }
            if (g_prefs.fsc.debug && now - lastRx > std::chrono::seconds(5) && now - lastDiag > std::chrono::seconds(5)) {
                logLine("FSC: no data for " +
                        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - lastRx).count()) +
                        "s (packets=" + std::to_string(packets) + ", bad_reads=" + std::to_string(badReads) + ")");
                lastDiag = now;
            }
            if (now - lastPoll > std::chrono::seconds(1)) {
                fscSendPoll();
                lastPoll = now;
            }
            continue;
        }
        if (r1 < 0) {
            ++badReads;
#if IBM
            DWORD e = GetLastError();
            logLine("FSC: read error (" + std::to_string(e) + "): " + win32ErrorMessage(e) + " (reconnecting)");
#else
            int e = errno;
            logLine("FSC: read error (" + std::to_string(e) + "): " + std::strerror(e) + " (reconnecting)");
#endif
            {
                std::lock_guard<std::mutex> lock(g_fscIoMutex);
                intptr_t cur = g_fscFd.exchange(-1);
                if (cur >= 0) {
                    closeFscPort(cur);
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        if (rawCaptureActive) {
            if (rawBytes >= 2048 || now > rawCaptureUntil) {
                flushRaw(now, true);
            } else {
                if (!rawLine.empty()) {
                    rawLine.push_back(' ');
                }
                rawLine += hexByte(b1);
                ++rawBytes;
                ++rawLineBytes;
                flushRaw(now, false);
            }
        }
        if (!(b1 & 0x80)) {
            continue;  // not a packet start
        }
        uint8_t b2 = 0;
        currentHandle = g_fscFd.load();
        if (currentHandle < 0) {
            continue;
        }
        int r2 = readByteWithTimeout(currentHandle, b2, 100);
        if (r2 <= 0) {
            if (r2 < 0) {
                ++badReads;
            }
            continue;
        }
        ++packets;
        lastRx = now;

        uint8_t cmd = static_cast<uint8_t>(b1 & 0x7E);
        if ((cmd != 0x12) && (cmd != 0x16) && (cmd != 0x20) && (cmd != 0x22) && (cmd != 0x24) && (cmd != 0x26) &&
            (cmd != 0x2A) && (cmd != 0x10) && (cmd != 0x2C)) {
            if (g_prefs.fsc.debug && cmd < unknownLogged.size() && !unknownLogged[cmd]) {
                logLine("FSC: unknown packet cmd=" + hexByte(cmd) + " b1=" + hexByte(b1) + " b2=" + hexByte(b2));
                unknownLogged[cmd] = true;
            }
        }

        handleFscPacket(b1, b2);

        auto pollNow = std::chrono::steady_clock::now();
        if (pollNow - lastPoll > std::chrono::seconds(1)) {
            fscSendPoll();
            lastPoll = pollNow;
        }
    }
    flushRaw(std::chrono::steady_clock::now(), true);
    {
        std::lock_guard<std::mutex> lock(g_fscIoMutex);
        intptr_t cur = g_fscFd.exchange(-1);
        if (cur >= 0) {
            closeFscPort(cur);
        }
    }
}

void startFsc() {
    if (!g_prefs.fsc.enabled || !g_fscProfileActive.load()) {
        return;
    }
    if (g_fscRunning.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_fscMutex);
        g_fscState = FscState{};
    }
    g_fscOut = FscOutputState{};
    g_fscPrev = FscPrev{};
    for (auto& s : g_fscSwitchState) {
        s.known = false;
        s.value = false;
    }
    g_fscMotorThrottleActive.store(false);
    g_fscMotorSpeedbrakeActive.store(false);
    g_fscRunning.store(true);
    g_fscThread = std::thread(fscLoop);
    g_fscActiveProfileId = g_fscProfileId;
}

void stopFsc() {
    if (!g_fscRunning.load()) {
        return;
    }
    g_fscRunning.store(false);
    {
        std::lock_guard<std::mutex> lock(g_fscIoMutex);
        intptr_t cur = g_fscFd.exchange(-1);
        if (cur >= 0) {
            closeFscPort(cur);
        }
    }
    if (g_fscThread.joinable()) {
        g_fscThread.join();
    }
    g_fscActiveProfileId.clear();
}

static void updateFscLifecycle(const char* /*reason*/) {
    bool shouldRun = g_pluginEnabled && g_prefs.fsc.enabled && g_fscProfileActive.load();
    if (shouldRun) {
        if (g_fscRunning.load() && g_fscActiveProfileId != g_fscProfileId) {
            stopFsc();
        }
        startFsc();
    } else {
        stopFsc();
    }
}

static void reloadPrefs() {
    bool wasEnabled = g_pluginEnabled;
    stopFsc();
    {
        std::lock_guard<std::mutex> lock(g_fscCalibMutex);
        g_fscCalib.active = false;
    }

    g_prefs = loadPrefs();
    openLogFileFromPrefs();
    logLine("Prefs reloaded from " + getPrefsPath());
    logFscSettings();
    syncFscWindowFromPrefs();
    loadFscProfiles();
    refreshFscProfile(true);

    if (wasEnabled) {
        updateFscLifecycle("prefs reload");
        logLine("Prefs reload complete.");
    } else {
        logLine("Prefs reload complete (plugin disabled).");
    }
}

int reloadPrefsCommandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase != xplm_CommandBegin) {
        return 1;
    }
    if (cmd == g_cmdReloadPrefs) {
        reloadPrefs();
    }
    return 1;
}

static void fscPluginStartCommon(bool registerFlightLoop) {
    g_prefs = loadPrefs();
    logLine("Prefs loaded from " + getPrefsPath());
    openLogFileFromPrefs();
    logLine(std::string("Plugin version ") + kPluginVersion);
    logFscSettings();

    loadFscProfiles();
    refreshFscProfile(false);

    auto cmdName = [](const char* suffix) {
        return std::string(PLUGIN_COMMAND_PREFIX) + "/" + suffix;
    };
    std::string cmdStart = cmdName("fsc_calib_start");
    std::string cmdNext = cmdName("fsc_calib_next");
    std::string cmdCancel = cmdName("fsc_calib_cancel");
    std::string cmdReload = cmdName("reload_prefs");
    std::string cmdReloadDesc = "Reload " + std::string(PLUGIN_PREFS_FILE) + " and reinitialize connections";
    g_cmdFscCalibStart = XPLMCreateCommand(cmdStart.c_str(), "Start FSC throttle quadrant calibration");
    g_cmdFscCalibNext = XPLMCreateCommand(cmdNext.c_str(), "Next step in FSC calibration");
    g_cmdFscCalibCancel = XPLMCreateCommand(cmdCancel.c_str(), "Cancel FSC calibration");
    g_cmdReloadPrefs = XPLMCreateCommand(cmdReload.c_str(), cmdReloadDesc.c_str());
    XPLMRegisterCommandHandler(g_cmdFscCalibStart, fscCalibCommandHandler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmdFscCalibNext, fscCalibCommandHandler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmdFscCalibCancel, fscCalibCommandHandler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmdReloadPrefs, reloadPrefsCommandHandler, 1, nullptr);
    createPluginMenu();

    if (registerFlightLoop) {
        XPLMRegisterFlightLoopCallback(flightLoopCallback, -1.0f, nullptr);
    }
    logLine("Started");
}

static void fscPluginStopCommon(bool unregisterFlightLoop) {
    stopFsc();
    destroyFscWindow();
    destroyPluginMenu();
    {
        std::lock_guard<std::mutex> lock(g_fscCalibMutex);
        g_fscCalib.active = false;
    }
    if (g_cmdFscCalibStart) {
        XPLMUnregisterCommandHandler(g_cmdFscCalibStart, fscCalibCommandHandler, 1, nullptr);
        g_cmdFscCalibStart = nullptr;
    }
    if (g_cmdFscCalibNext) {
        XPLMUnregisterCommandHandler(g_cmdFscCalibNext, fscCalibCommandHandler, 1, nullptr);
        g_cmdFscCalibNext = nullptr;
    }
    if (g_cmdFscCalibCancel) {
        XPLMUnregisterCommandHandler(g_cmdFscCalibCancel, fscCalibCommandHandler, 1, nullptr);
        g_cmdFscCalibCancel = nullptr;
    }
    if (g_cmdReloadPrefs) {
        XPLMUnregisterCommandHandler(g_cmdReloadPrefs, reloadPrefsCommandHandler, 1, nullptr);
        g_cmdReloadPrefs = nullptr;
    }
    if (unregisterFlightLoop) {
        XPLMUnregisterFlightLoopCallback(flightLoopCallback, nullptr);
    }
    if (g_fileLog.is_open()) {
        g_fileLog.close();
    }
}

static void fscPluginDisableCommon() {
    g_pluginEnabled = false;
    stopFsc();
    logLine("Disabled");
}

static int fscPluginEnableCommon() {
    g_pluginEnabled = true;
    updateFscLifecycle("plugin enable");
    logLine("Enabled");
    return 1;
}

static void fscPluginReceiveMessageCommon(int inMessage) {
    if (inMessage == XPLM_MSG_AIRPORT_LOADED || inMessage == XPLM_MSG_PLANE_LOADED) {
        refreshFscProfile(true);
        updateFscLifecycle("aircraft load");
    }
}

}  // namespace

#if defined(FSC_EMBEDDED)
void FscEmbedded_Start() {
    fscPluginStartCommon(true);
}

void FscEmbedded_Stop() {
    fscPluginStopCommon(true);
}

void FscEmbedded_Enable() {
    fscPluginEnableCommon();
}

void FscEmbedded_Disable() {
    fscPluginDisableCommon();
}

void FscEmbedded_OnMessage(int inMessage) {
    fscPluginReceiveMessageCommon(inMessage);
}

void FscEmbedded_ReloadPrefs() {
    reloadPrefs();
}
#endif

#if !defined(FSC_EMBEDDED)
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    std::strncpy(outName, PLUGIN_NAME, 255);
    outName[255] = '\0';
    std::strncpy(outSig, PLUGIN_SIGNATURE, 255);
    outSig[255] = '\0';
    std::string desc = std::string(PLUGIN_DESC) + " (v" + kPluginVersion + ")";
    std::strncpy(outDesc, desc.c_str(), 255);
    outDesc[255] = '\0';

    fscPluginStartCommon(true);
    return 1;
}

PLUGIN_API void XPluginStop() {
    fscPluginStopCommon(true);
}

PLUGIN_API void XPluginDisable() {
    fscPluginDisableCommon();
}

PLUGIN_API int XPluginEnable() {
    return fscPluginEnableCommon();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*inFromWho*/, int inMessage, void* /*inParam*/) {
    fscPluginReceiveMessageCommon(inMessage);
}
#endif
