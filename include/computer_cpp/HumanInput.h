#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace ComputerCpp::HumanInput {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct MotionPlan {
    int durationMs = 180;
    int steps = 18;
};

struct ScrollCluster {
    int deltaY = 0;
    int deltaX = 0;
    int durationMs = 180;
    int steps = 8;
    int pauseAfterMs = 0;
};

class Random {
public:
    Random() : rng_(std::random_device{}()) {}

    int Int(int low, int high) {
        std::uniform_int_distribution<int> dist(low, high);
        return dist(rng_);
    }

    double Double(double low, double high) {
        std::uniform_real_distribution<double> dist(low, high);
        return dist(rng_);
    }

private:
    std::mt19937 rng_;
};

inline Random& ThreadRandom() {
    static thread_local Random rng;
    return rng;
}

inline MotionPlan PlanPointerMove(double distance, int requestedDurationMs, int requestedSteps, Random& rng = ThreadRandom()) {
    MotionPlan plan;
    if (requestedDurationMs > 0) {
        plan.durationMs = requestedDurationMs;
    } else if (distance < 24.0) {
        plan.durationMs = rng.Int(55, 115);
    } else if (distance < 120.0) {
        plan.durationMs = static_cast<int>(std::clamp(distance * rng.Double(1.7, 2.5), 95.0, 260.0));
    } else {
        plan.durationMs = static_cast<int>(std::clamp(distance * rng.Double(0.55, 0.88), 220.0, 1050.0));
    }

    if (requestedSteps > 1) {
        plan.steps = requestedSteps;
    } else if (distance < 24.0) {
        plan.steps = rng.Int(5, 10);
    } else {
        int base = static_cast<int>(std::round(static_cast<double>(plan.durationMs) / rng.Double(13.0, 19.0)));
        plan.steps = std::clamp(base, 10, 72);
    }

    plan.durationMs = std::clamp(plan.durationMs, 45, 1600);
    plan.steps = std::clamp(plan.steps, 2, 80);
    return plan;
}

inline MotionPlan PlanScrollGesture(int deltaY, int deltaX, int requestedDurationMs, int requestedSteps, Random& rng = ThreadRandom()) {
    int distance = std::max(std::abs(deltaY), std::abs(deltaX));
    MotionPlan plan;
    if (requestedDurationMs > 0) {
        plan.durationMs = requestedDurationMs;
    } else if (distance < 120) {
        plan.durationMs = rng.Int(95, 170);
    } else {
        plan.durationMs = static_cast<int>(std::clamp(static_cast<double>(distance) * rng.Double(1.15, 1.85), 240.0, 1350.0));
    }

    if (requestedSteps > 1) {
        plan.steps = requestedSteps;
    } else if (distance < 120) {
        plan.steps = rng.Int(3, 7);
    } else {
        int base = static_cast<int>(std::round(static_cast<double>(plan.durationMs) / rng.Double(24.0, 38.0)));
        plan.steps = std::clamp(base, 8, 56);
    }

    plan.durationMs = std::clamp(plan.durationMs, 50, 2500);
    plan.steps = std::clamp(plan.steps, 1, 80);
    return plan;
}

inline std::vector<ScrollCluster> PlanScrollClusters(
    int deltaY,
    int deltaX,
    int requestedDurationMs,
    int requestedSteps,
    int maxClusterDelta,
    Random& rng = ThreadRandom()
) {
    int distance = std::max(std::abs(deltaY), std::abs(deltaX));
    int safeMaxDelta = std::clamp(maxClusterDelta, 24, 800);
    int clusterCount = distance > 0 ? static_cast<int>(std::ceil(static_cast<double>(distance) / safeMaxDelta)) : 1;
    clusterCount = std::clamp(clusterCount, 1, 24);

    std::vector<ScrollCluster> clusters;
    clusters.reserve(static_cast<size_t>(clusterCount));
    int prevY = 0;
    int prevX = 0;
    for (int i = 1; i <= clusterCount; ++i) {
        int nextY = static_cast<int>(std::llround(static_cast<double>(deltaY) * i / clusterCount));
        int nextX = static_cast<int>(std::llround(static_cast<double>(deltaX) * i / clusterCount));
        int chunkY = nextY - prevY;
        int chunkX = nextX - prevX;
        prevY = nextY;
        prevX = nextX;
        if (chunkY == 0 && chunkX == 0) continue;

        int duration = requestedDurationMs > 0
            ? std::max(90, requestedDurationMs / clusterCount)
            : 0;
        int steps = requestedSteps > 1
            ? std::max(2, requestedSteps / clusterCount)
            : 0;
        auto plan = PlanScrollGesture(chunkY, chunkX, duration, steps, rng);
        clusters.push_back({
            chunkY,
            chunkX,
            plan.durationMs,
            plan.steps,
            i < clusterCount ? rng.Int(70, 165) : 0,
        });
    }
    if (clusters.empty()) {
        auto plan = PlanScrollGesture(deltaY, deltaX, requestedDurationMs, requestedSteps, rng);
        clusters.push_back({deltaY, deltaX, plan.durationMs, plan.steps, 0});
    }
    return clusters;
}

inline std::vector<Point> CurvedPath(Point start, Point end, int steps, Random& rng = ThreadRandom()) {
    constexpr double pi = 3.14159265358979323846;
    int safeSteps = std::clamp(steps, 2, 120);
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    double distance = std::hypot(dx, dy);
    double normalX = distance > 0.0 ? -dy / distance : 0.0;
    double normalY = distance > 0.0 ? dx / distance : 0.0;
    double bow = std::clamp(distance * rng.Double(-0.12, 0.12), -95.0, 95.0);
    double c1x = start.x + dx * rng.Double(0.24, 0.39) + normalX * bow;
    double c1y = start.y + dy * rng.Double(0.24, 0.39) + normalY * bow;
    double c2x = start.x + dx * rng.Double(0.62, 0.82) - normalX * bow * rng.Double(0.25, 0.75);
    double c2y = start.y + dy * rng.Double(0.62, 0.82) - normalY * bow * rng.Double(0.25, 0.75);

    std::vector<Point> points;
    points.reserve(static_cast<size_t>(safeSteps));
    for (int i = 1; i <= safeSteps; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(safeSteps);
        double eased = t * t * (3.0 - 2.0 * t);
        double u = 1.0 - eased;
        double wobble = distance > 80.0 && i < safeSteps ? std::sin(pi * t) * rng.Double(-0.7, 0.7) : 0.0;
        points.push_back({
            u * u * u * start.x + 3.0 * u * u * eased * c1x + 3.0 * u * eased * eased * c2x + eased * eased * eased * end.x + normalX * wobble,
            u * u * u * start.y + 3.0 * u * u * eased * c1y + 3.0 * u * eased * eased * c2y + eased * eased * eased * end.y + normalY * wobble,
        });
    }
    return points;
}

inline int ScaledDelayMs(int baseMs, double low = 0.75, double high = 1.28, Random& rng = ThreadRandom()) {
    return std::max(1, static_cast<int>(std::round(static_cast<double>(baseMs) * rng.Double(low, high))));
}

inline int PreClickSettleMs(Random& rng = ThreadRandom()) {
    return rng.Int(38, 115);
}

inline int ClickHoldMs(Random& rng = ThreadRandom()) {
    return rng.Int(48, 132);
}

inline int MultiClickGapMs(Random& rng = ThreadRandom()) {
    return rng.Int(70, 145);
}

inline bool ShouldMicroPause(double distance, Random& rng = ThreadRandom()) {
    return distance > 80.0 && rng.Int(1, 100) <= 18;
}

inline int MicroPauseMs(Random& rng = ThreadRandom()) {
    return rng.Int(35, 90);
}

} // namespace ComputerCpp::HumanInput
