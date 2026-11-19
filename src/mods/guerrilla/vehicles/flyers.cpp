// Guerrilla's flight model for its flyers when NPCs fly them (flyer_control_action::applyAction and
// flyer::flight_compute_physics, which Armageddon dropped: an NPC's flyer is a free body there and falls),
// ported onto Armageddon's own flyer fields. A part of guerrilla_vehicles.
//
// Armageddon kept everything around it: the AI's requests (urgency, top speed, a point to fly to and whether
// to stop there, halt, a look-at; Guerrilla's fields at +0x1038 in its flyer, +0x50F8 on, which its path
// following still fills every frame through 0x61D2F0 / 0x61D370 when the game's vehicle driving 0x4B18E0
// runs for a flyer), update_flight_info (0x61D1B0: mass, centre of mass, the thrust matrices), the hover
// noise and the engine's start (its startup timer bringing engine_force to 1 after 2.5 s). Only the model
// that turns them into flight went. It runs here once a frame, after each Guerrilla flyer's own presim
// (vehicle_do_frame_presim 0x60A120, wrapped), just before the physics steps, for a flyer whose pilot isn't
// the player (the player's flight is Armageddon's, with Guerrilla's limits in its fields); its engine is
// started when an NPC sits at its controls (Guerrilla's NPCs take off with it running). With dt the frame's
// time, as Guerrilla's per physics step (it sets the velocities directly, so a frame's step is the same):
// - the linear part: an acceleration limit a = √((u·T + g)² − g²) in every direction (T the up thrust in g's,
//   u the urgency, Guerrilla's 0.6), toward the point (its drift killed first; along the way Guerrilla's
//   target curve: speed up to the top speed, coast, brake to stop at the point when asked), set as the new
//   velocity, and the thrust axis leaned into it (forward by ThrustTiltScalar, sideways by BankTiltScalar);
// - yaw toward the look-at (with none: the way it flies, plus 0.9 of the way to the point, once it's faster
//   than √6 m/s; slower, the turning damped), the shorter or the quicker way round, by the target curve at
//   TurnAngvel and TurnAngaccl (revolutions, × 2π u); pitch to the look-at's height (±ThrustOffsetX), bank with
//   the turn (only with a real look-at, ±ThrustOffsetZ);
// - the tilt: the thrust axis turned onto its target about a level axis at TiltAngvel and TiltAngaccl;
// - a correction impulse: upside down it falls freely, and with too little thrust it sinks;
// - the anti-gravity impulse, engine_force × mass × g × dt up (cancelling the world's gravity while the
//   engine runs), and the turbine's rpm target for the engine's sound.
// Guerrilla's values come from mods\guerrilla_vehicles\flyers.txt (GuerrillaBuilder.exe writes them: Armageddon's
// reader keeps some only as whole degrees, and the player's limits overwrite two). Kept as Guerrilla has it:
// one branch of the tilt adds the off-axis turn instead of cancelling it.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gv {

namespace {

struct Model {
    float upThrust, turnAngvel, turnAngaccl, thrustOffsetX, thrustOffsetZ, tiltAngvel, tiltAngaccl, spinBank, thrustTilt, bankTilt, lookatY;
};
std::vector<std::pair<std::string, Model>> g_models;
bool g_on = false;

// Armageddon's flyer (Guerrilla's flyer fields + 0x1038).
constexpr DWORD kUrgency = 0x50F8, kMaxVel = 0x50FC, kReqPoint = 0x5100, kReqVel = 0x510C, kReqLookat = 0x5118, kPointValid = 0x5124,
                kVelValid = 0x5125, kLookatValid = 0x5126, kStopAtPoint = 0x5127, kHalt = 0x5128, kMass = 0x512C, kComWs = 0x513C,
                kThrustCs = 0x5148, kThrustWs = 0x516C, kThrustUpTarget = 0x5190, kHoverNoise = 0x519C, kRpmTarget = 0x51CC, kEngineForce = 0x520C;
constexpr DWORD kEngineRunning = 1u << 13;
constexpr float kG = 9.8f, kTwoPi = 6.2831855f, kUrgencyGuerrilla = 0.6f, kMinLookatVelSq = 6.0f;
const float* const kFrameTime = reinterpret_cast<const float*>(0x11217C8);

struct M3 { V3 r, u, f; };  // a matrix: right, up, forward

auto const UpdateFlightInfo = reinterpret_cast<void(__fastcall*)(BYTE* flyer, void* unused)>(0x61D1B0);
auto const FlyerEngineStart = reinterpret_cast<void(__fastcall*)(BYTE* flyer, void* unused, bool quiet)>(0x61D520);
auto const WorldThrustMatrix = reinterpret_cast<void(__cdecl*)(const M3* orient, const V3* upCs, M3* thrustCs, M3* thrustWs)>(0x61ED90);
auto const MakeIdentity = reinterpret_cast<void(__fastcall*)(M3* m, void* unused)>(0xC9D8C0);
auto const RotateAroundLocalZ = reinterpret_cast<void(__fastcall*)(M3* m, void* unused, float angle)>(0xC9E800);
auto const RotateAroundLocalX = reinterpret_cast<void(__fastcall*)(M3* m, void* unused, float angle)>(0xC9E6F0);
auto const HavokBody = reinterpret_cast<BYTE*(__cdecl*)(DWORD handle)>(0x778440);  // the body of a handle, null for −1
auto const BodyPosOrient = reinterpret_cast<void(__cdecl*)(BYTE* body, V3* pos, M3* orient)>(0x776520);
auto const BodyVelocity = reinterpret_cast<void(__cdecl*)(BYTE* body, V3* out)>(0x776770);
auto const BodySpin = reinterpret_cast<void(__cdecl*)(BYTE* body, V3* out)>(0x776DB0);
auto const SetBodyVelocity = reinterpret_cast<void(__cdecl*)(BYTE* body, const V3* v)>(0x7767D0);
auto const SetBodySpin = reinterpret_cast<void(__cdecl*)(BYTE* body, const V3* w)>(0x776C70);
auto const BodyImpulse = reinterpret_cast<void(__cdecl*)(BYTE* body, const V3* impulse)>(0x776F70);

// ---- Guerrilla's vector helpers
V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator-(const V3& a) { return {-a.x, -a.y, -a.z}; }
V3 operator*(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Mag(const V3& a) { return std::sqrt(Dot(a, a)); }
V3 NormalizeSafe(const V3& v) {  // vector::normalize_safe: (1, 0, 0) when it has no length
    const float m = Mag(v);
    return m < 1e-7f ? V3{1, 0, 0} : v * (1.0f / m);
}
V3 FlattenNormalizeSafe(V3 v) { v.y = 0; return NormalizeSafe(v); }
float MagAndNormalizeSafe(V3& v) {  // its length (0 when it set (1, 0, 0)), and v made a unit
    const float m = Mag(v);
    if (m < 1e-7f) { v = {1, 0, 0}; return 0; }
    v = v * (1.0f / m);
    return m;
}
bool GreaterOrEqualRobust(float a, float b, float eps) {  // fl_greater_than_equal_robust
    const float m = (std::max)((std::max)(std::fabs(a), std::fabs(b)), 1.0f);
    return a >= b - m * eps;
}
bool ZeroRobust(float a) {
    const float eps = (std::max)(std::fabs(a), 1.0f) * 0.0001f;
    return a >= -eps && eps >= a;
}
float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
// Nothing that isn't a number reaches the physics (a body with one would be lost to Havok).
bool Finite(const V3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
bool Usable(const V3& a, const char* what) {
    if (Finite(a)) return true;
    RFA_LOG_FIRST(5, "Flyers: the flight model gave a %s that isn't a number; left out this frame", what);
    return false;
}

// flyer_compute_target_curve: the acceleration along x (to go) at rate v, top rate vmax, limit accel.
float TargetCurve(float x, float v, float vmax, float accel, float dt) {
    const float ax = std::fabs(x), av = std::fabs(v), invDt = 1.0f / dt, adt = accel * dt;
    float r;
    if (0.005f > x && 0.005f > -x) {  // at the target
        if (0.001f > v && 0.001f > -v) return 0.0f;
        r = adt > av ? invDt * av : accel;
        return v >= 0 ? -r : r;
    }
    if (0.001f > v && 0.001f > -v) {  // at rest, away from it
        r = adt > ax ? invDt * ax : accel;
        return x >= 0 ? r : -r;
    }
    if (0.0 > static_cast<double>(x) * v) {  // moving away: turn round
        r = accel;
        if (adt > av) {
            const float s = std::sqrt(ax * accel);
            if ((adt - av) * invDt > s) r = (s + av) * invDt;
        }
        return v >= 0 ? -r : r;
    }
    if (av > vmax * 1.05f) { r = accel * 0.8f; return v >= 0 ? -r : r; }  // too fast
    const float stopping = av * 0.5f * av / accel;
    if (stopping >= ax) { r = adt > av ? invDt * av : accel; return v >= 0 ? -r : r; }  // brake
    if (stopping * 2.0f > ax) return 0.0f;                                              // coast
    if (GreaterOrEqualRobust(av, vmax, 0.001f)) return 0.0f;                            // cruise
    return v >= 0 ? accel : -accel;                                                     // speed up
}

void TimeDeltaV(float dv, float accel, float& t, float& d) {
    const float a = std::fabs(dv);
    t = a / accel;
    d = a * 0.5f * t;
}
void TimeStartStop(float x, float accel, float vmax, float& t) {  // sets t (a triangle) or adds to it (a trapezoid), as Guerrilla's
    const float ax = std::fabs(x), peak = std::sqrt(ax * accel);
    if (vmax >= peak) { t = peak / accel * 2.0f; return; }
    const float t1 = std::fabs(vmax) / accel, d1 = std::fabs(vmax) * 0.5f * t1;
    t = t + t1 * 2.0f + (ax - d1 * 2.0f) / vmax;
}
float TargetTime(float x, float v, float vmax, float accel) {  // flyer_compute_target_time
    if (ZeroRobust(x)) {
        if (ZeroRobust(v)) return 0.0f;
        float t, d;
        TimeDeltaV(v, accel, t, d);
        const float total = t;
        TimeStartStop(d, accel, vmax, t);
        return t + total;
    }
    if (ZeroRobust(v)) {
        TimeStartStop(x, accel, vmax, v);  // Guerrilla's reuses v (about 0)
        return v;
    }
    float t, d, total, x2;
    TimeDeltaV(v, accel, t, d);
    if (0.0 > static_cast<double>(x) * v) { total = t; x2 = d + x; }
    else if (d > x) { total = t; x2 = d - x; }
    else { total = -t; x2 = d + x; }
    TimeStartStop(x2, accel, vmax, t);
    return t + total;
}

template <typename T> T& Field(BYTE* flyer, DWORD offset) { return *reinterpret_cast<T*>(flyer + offset); }

// flyer::flight_compute_physics, for one of Guerrilla's flyers.
void FlightComputePhysics(BYTE* flyer, BYTE* body, const Model& model, float dt) {
    if (0.0f > dt) dt = *kFrameTime;
    const float u = Field<float>(flyer, kUrgency);

    // A. Limits.
    const float T = model.upThrust * kG;
    const float aFull = std::sqrt((T + kG) * (T + kG) - kG * kG);
    const float a = std::sqrt((u * T + kG) * (u * T + kG) - kG * kG);
    const float turnVel = model.turnAngvel * kTwoPi * u, turnAcc1 = model.turnAngaccl * kTwoPi, turnAcc = turnAcc1 * u;
    const float tiltVel = model.tiltAngvel * kTwoPi * u, tiltAcc1 = model.tiltAngaccl * kTwoPi, tiltAcc = tiltAcc1 * u;
    V3 pos;
    M3 orient;
    BodyPosOrient(body, &pos, &orient);
    V3 v, w;
    BodyVelocity(body, &v);
    BodySpin(body, &w);

    // B. The point to fly to.
    V3& reqPoint = Field<V3>(flyer, kReqPoint);
    V3& reqVel = Field<V3>(flyer, kReqVel);
    V3& reqLookat = Field<V3>(flyer, kReqLookat);
    bool& pointValid = Field<bool>(flyer, kPointValid);
    bool& velValid = Field<bool>(flyer, kVelValid);
    bool& lookatValid = Field<bool>(flyer, kLookatValid);
    bool& stopAtPoint = Field<bool>(flyer, kStopAtPoint);
    bool& halt = Field<bool>(flyer, kHalt);
    const V3 com = Field<V3>(flyer, kComWs);
    V3 P = reqPoint + Field<V3>(flyer, kHoverNoise);
    if (halt) {
        reqVel = {0, 0, 0};
        if (0.01f > Dot(v, v)) pointValid = velValid = stopAtPoint = halt = false;  // stopped: nothing more
        else {                                                                     // brake to here (this step still aims at the old point)
            reqPoint = com;
            pointValid = true;
            velValid = false;
            stopAtPoint = true;
            halt = true;
        }
    }
    if (velValid) P = com + reqVel * 100.0f;

    // C. The velocity.
    float dv = a * dt;
    V3 A{0, 0, 0};
    V3& thrustUpTarget = Field<V3>(flyer, kThrustUpTarget);
    thrustUpTarget = {0, 1, 0};
    if (!Finite(v) || !Finite(w) || !Finite(com)) return;
    if (velValid || pointValid) {
        const float speed = Mag(v);
        const V3 vdir = speed > 0 ? v * (1.0f / speed) : V3{0, 0, 0};
        const V3 D = P - com;
        const float dist = Mag(D);
        if (0.004f > dist) A = speed > dv ? -vdir * a : -v;
        else {
            const V3 Ddir = D * (1.0f / dist);
            const float vproj = Dot(Ddir, v);
            const V3 perp = v - Ddir * vproj;
            const float perpMag = Mag(perp);
            if (Dot(D, v) > 0.0f) {  // closing in
                if (perpMag > dv) A = -perp * (a / perpMag);
                else {
                    const float rest = std::sqrt((std::max)(a * a - perpMag * perpMag, 0.0f));
                    const float c = TargetCurve(stopAtPoint ? dist : 1000000.0f, vproj, Field<float>(flyer, kMaxVel), rest, dt);
                    A = -perp + Ddir * c;
                }
            } else A = speed > dv ? -vdir * a : -perp + Ddir * std::sqrt((std::max)(a * a - perpMag * perpMag, 0.0f));
        }
        const V3 newV = v + A * dt;
        if (Usable(newV, "velocity")) SetBodyVelocity(body, &newV);
        // D. Lean the thrust into the acceleration.
        if (dist > 0.1f) {
            const V3 F = FlattenNormalizeSafe(orient.f);
            const V3 R = NormalizeSafe(Cross({0, 1, 0}, F));
            const V3 Af{A.x, 0.0f, A.z};
            const float side = Dot(R, Af) * model.bankTilt, forward = Dot(F, Af) * model.thrustTilt;
            thrustUpTarget = NormalizeSafe(V3{0, kG, 0} + R * side + F * forward);
        }
    }

    // E. Look-at, yaw, pitch, bank.
    const float yawRate = w.y;
    float pitch = 0.0f, bank = 0.0f, yawAcc = 0.0f;
    bool steer = true;
    if (!lookatValid) {
        const V3 flat{v.x, v.y * model.lookatY, v.z};
        if (Dot(flat, flat) > kMinLookatVelSq) {
            reqLookat = NormalizeSafe(flat);
            if (velValid || pointValid) reqLookat = reqLookat + NormalizeSafe(P - com) * 0.9f;  // not normalised again, as Guerrilla's
        } else {
            yawAcc = -0.5f * yawRate;
            steer = false;
        }
    }
    if (steer) {
        const V3 Ff = FlattenNormalizeSafe(orient.f), Lf = FlattenNormalizeSafe(reqLookat);
        const float angle = std::acos(std::clamp(Dot(Ff, Lf), -1.0f, 1.0f));
        const float s = Dot(orient.r, Lf);
        const float e1 = s >= 0 ? angle : -angle;
        const float e2 = s >= 0 ? -(kTwoPi - angle) : (kTwoPi - angle);
        const float t2 = TargetTime(e2, yawRate, turnVel, turnAcc), t1 = TargetTime(e1, yawRate, turnVel, turnAcc);
        yawAcc = TargetCurve(t2 - t1 < 0 ? e2 : e1, yawRate, turnVel, turnAcc, dt);
        pitch = std::clamp(std::asin(std::clamp(reqLookat.y, -1.0f, 1.0f)), -model.thrustOffsetX, model.thrustOffsetX);
        if (lookatValid) bank = std::clamp(2.0f * model.spinBank * yawRate, -model.thrustOffsetZ, model.thrustOffsetZ);
    }

    // F. The thrust frame, in the chassis's space, then the world's.
    M3& thrustCs = Field<M3>(flyer, kThrustCs);
    M3& thrustWs = Field<M3>(flyer, kThrustWs);
    MakeIdentity(&thrustCs, nullptr);
    RotateAroundLocalZ(&thrustCs, nullptr, bank);
    RotateAroundLocalX(&thrustCs, nullptr, pitch);
    const V3 upCs = thrustCs.u;
    WorldThrustMatrix(&orient, &upCs, &thrustCs, &thrustWs);

    // G. The tilt: the thrust axis U turned onto its target about a level axis K.
    V3 alpha{0, 0, 0};
    const float dvT = tiltAcc * dt;
    V3 wxz{w.x, 0.0f, w.z};
    const float wxzMag = MagAndNormalizeSafe(wxz);
    const V3 U = thrustWs.u, Tt = thrustUpTarget;
    const V3 B = Tt + U, C = Cross(U, Tt);
    const float sinA = Mag(C);
    V3 K = C;
    if (sinA >= 0.003f) {
        // Where the segment from B to C meets the level plane through the origin (ix_linesegment_plane).
        const V3 d = C - B;
        const float den = -d.y;
        if (den != 0.0f) {
            const float t = B.y / den;
            if (t >= 0.0f && t <= 1.0f) K = B + d * t;
        }
        K = NormalizeSafe(K);
    }
    if (0.003f > sinA) alpha = wxzMag > dvT ? wxz * -tiltAcc : wxz * -(wxzMag / dt);
    else {
        const V3 wx = wxz * wxzMag;
        const float proj = Dot(K, wx);
        V3 Wp{w.x - K.x * proj, 0.0f - K.y * proj, w.z - K.z * proj};
        const float WpMag = MagAndNormalizeSafe(Wp);
        if (proj > 0.0f) {
            if (WpMag > dvT) alpha = Wp * tiltAcc;  // as Guerrilla's (it adds the off-axis turn here)
            else {
                const V3 Up = NormalizeSafe(U - K * Dot(K, U)), Tp = NormalizeSafe(Tt - K * Dot(K, Tt));
                const float theta = std::acos(std::clamp(Dot(Up, Tp), -1.0f, 1.0f));
                const float r = TargetCurve(theta, proj, tiltVel, std::sqrt((std::max)(dvT * dvT - WpMag * WpMag, 0.0f)) / dt, dt);
                alpha = K * r + Wp * (-WpMag / dt);
            }
        } else if (wxzMag > 2.0f * dvT) alpha = wxz * (-2.0f * tiltAcc);
        else alpha = K * std::sqrt((std::max)(4.0f * dvT * dvT - WpMag * WpMag, 0.0f)) + Wp * (-WpMag / dt);
    }
    const V3 newW{w.x + alpha.x * dt, w.y + yawAcc * dt, w.z + alpha.z * dt};
    if (Usable(newW, "spin")) SetBodySpin(body, &newW);

    // H. The correction impulse: what the thrust can't give.
    V3 I{0, 0, 0};
    if (0.0f > U.y) I = {-A.x, -kG - A.y, -A.z};  // upside down: no thrust
    else {
        const V3 thrust = U * (T + kG);
        if (0.0f > thrust.y - kG) I = {thrust.x - A.x, thrust.y - kG - A.y, thrust.z - A.z};  // can't hold its height
    }
    const V3 impulse = I * (Field<float>(flyer, kMass) * dt);
    if (Usable(impulse, "correction")) BodyImpulse(body, &impulse);

    // I. The turbine's rpm target (its engine's sound).
    const float y = std::clamp(A.y + aFull, 0.0f, 100000.0f);
    float rpm = 800.0f + 2200.0f * Clamp01(y / (aFull * 2.0f));
    rpm += 1540.0f * Clamp01(std::sqrt(A.x * A.x + A.z * A.z) / aFull);
    rpm += 1100.0f * Clamp01(Mag(alpha) / tiltAcc1);
    rpm += 1100.0f * Clamp01(yawAcc / turnAcc1);
    rpm += 3520.0f * Clamp01(Mag(I) / kG);
    if (std::isfinite(rpm)) Field<float>(flyer, kRpmTarget) = std::clamp(rpm, 800.0f, 5200.0f);
}

const Model* ModelOf(BYTE* vehicle) {
    BYTE* info = *reinterpret_cast<BYTE**>(vehicle + 0x3DA8);
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (!name) return nullptr;
    for (const auto& [model, values] : g_models)
        if (_stricmp(model.c_str(), name) == 0) return &values;
    return nullptr;
}

// After a vehicle's presim (cdecl, the vehicle), each frame before the physics steps.
constexpr DWORD kPresim = 0x60A120;
constexpr size_t kPresimPrologue = 6;  // push ebx / mov ebx,esp / sub esp,8
using PresimFn = void(__cdecl*)(BYTE* vehicle);
PresimFn g_presim;

void __cdecl Presim(BYTE* vehicle) {
    g_presim(vehicle);
    if (!vehicle || VehicleSubtype(vehicle, nullptr) != kFlyer) return;
    const Model* model = ModelOf(vehicle);
    if (!model) return;
    BYTE* pilot = SeatOccupant(vehicle, 0);
    if (pilot && pilot == Player()) return;  // the player's flight is Armageddon's
    // Its Havok body, looked up as the game does (0x778440: the handle at +0x4C, or null for −1). A vehicle
    // without one yet (or any more) still gets its presim, with −1 there; update_flight_info then reads its mass
    // through null (0x5F19F0 → 0xACA3B0: the 22:08:21 crash, reading 0x1A0, as the EDF's fly-by came in).
    BYTE* body = HavokBody(*reinterpret_cast<const DWORD*>(vehicle + 0x4C));
    if (!body) return;
    const DWORD flags = *reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags);
    if (pilot && !(flags & kEngineRunning)) {
        FlyerEngineStart(vehicle, nullptr, false);  // its pilot takes off with it running
        RFA_LOG_DETAIL_FIRST(10, "Flyers: an NPC's flyer's engine started");
    }
    const float dt = *kFrameTime;
    if (dt <= 0.0f) return;
    UpdateFlightInfo(vehicle, nullptr);
    if (*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags) & kEngineRunning) {
        Field<float>(vehicle, kUrgency) = kUrgencyGuerrilla;  // Guerrilla's flyer_default_urgency
        FlightComputePhysics(vehicle, body, *model, dt);
        RFA_LOG_FIRST(1, "Flyers: Guerrilla's flight model flies an NPC's flyer");
    }
    const float force = Field<float>(vehicle, kEngineForce);
    if (force > 0.0f) {  // anti-gravity, as Guerrilla's applyAction
        const V3 up{0, force * Field<float>(vehicle, kMass) * kG * dt, 0};
        BodyImpulse(body, &up);
    }
}

bool ReadModels() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_vehicles\\flyers.txt");
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string name;
        Model m{};
        if (in >> name >> m.upThrust >> m.turnAngvel >> m.turnAngaccl >> m.thrustOffsetX >> m.thrustOffsetZ >> m.tiltAngvel >> m.tiltAngaccl >> m.spinBank >>
            m.thrustTilt >> m.bankTilt >> m.lookatY && m.turnAngaccl > 0 && m.tiltAngaccl > 0)
            g_models.push_back({name, m});
    }
    return !g_models.empty();
}

}  // namespace

bool FlyersFly() { return g_on; }

void StartFlyers() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"NpcFlight", 1)) { rfa::Log("Flyers: Guerrilla's flight model off ([GuerrillaVehicles] NpcFlight=0)"); return; }
    if (!ReadModels()) { rfa::Log("Flyers: no mods\\guerrilla_vehicles\\flyers.txt (build the vehicles); NPCs can't fly Guerrilla's flyers"); return; }
    const BYTE opening[] = {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08};
    if (memcmp(reinterpret_cast<const void*>(kPresim), opening, sizeof(opening)) != 0) {
        rfa::Log("Flyers: the game's vehicle presim isn't as expected; NPCs can't fly Guerrilla's flyers");
        return;
    }
    g_presim = reinterpret_cast<PresimFn>(rfa::HookFunction(kPresim, kPresimPrologue, reinterpret_cast<void*>(&Presim)));
    g_on = g_presim != nullptr;
    rfa::Log(g_on ? "Flyers: Guerrilla's flight model for NPCs' flyers (%u of Guerrilla's)" : "Flyers: could not hook the vehicles' presim (%u flyers)",
             static_cast<unsigned>(g_models.size()));
}

}  // namespace gv
