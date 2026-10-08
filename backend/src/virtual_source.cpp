#include "virtual_source.h"

#include <algorithm>

#include "pool.h"

namespace merging {

namespace {

// 高峰窗内取起点：均匀分布，且至少给 90 分钟窗口留出空间（保证同批乘客窗口大量重叠、便于成团）
int pick_start_min(std::mt19937& rng) {
    std::uniform_int_distribution<int> peak_pick(0, 1);
    const bool morning = peak_pick(rng) == 0;
    const int lo =
        morning ? VirtualGenerator::kMorningPeakStart : VirtualGenerator::kEveningPeakStart;
    const int hi = morning ? VirtualGenerator::kMorningPeakEnd : VirtualGenerator::kEveningPeakEnd;
    const int span = std::max(hi - lo - 90, 0);
    std::uniform_int_distribution<int> offset_dist(0, span);
    return lo + offset_dist(rng);
}

}  // namespace

std::vector<int> VirtualGenerator::generate(int count, const std::string& date, std::mt19937& rng) {
    std::vector<int> ids;
    ids.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        ids.push_back(pool_->add(make_one(rng, date)));
    }
    return ids;
}

Passenger VirtualGenerator::make_one(std::mt19937& rng, const std::string& date) const {
    std::uniform_int_distribution<int> duration_dist(30, 150);
    std::discrete_distribution<int> party_dist({55, 25, 12, 8});  // 1/2/3/4 人
    std::bernoulli_distribution gender_dist(0.5);                 // male / female 各半
    std::discrete_distribution<int> pref_dist({70, 20, 10});      // none/female_only/male_only

    Passenger p;
    p.is_virtual = true;
    p.party_size = party_dist(rng) + 1;
    p.gender = gender_dist(rng) ? Gender::kFemale : Gender::kMale;
    switch (pref_dist(rng)) {
        case 1:
            p.pref = GenderPref::kFemaleOnly;
            break;
        case 2:
            p.pref = GenderPref::kMaleOnly;
            break;
        default:
            p.pref = GenderPref::kNone;
            break;
    }
    const int start = pick_start_min(rng);
    p.win.date = date;
    p.win.start_min = start;
    p.win.end_min = std::min(start + duration_dist(rng), 1440);
    return p;
}

}  // namespace merging
