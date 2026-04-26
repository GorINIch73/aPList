#pragma once

#include <string>
#include "models.hpp"

namespace waysheet::fuel {

std::string detectSeason(int month);
double calcNormativeFuel(double distance_km, double route_norm_l100);
double calcActualFuel(double fuel_start_l, double fuel_added_l, double fuel_end_l);
double calcVariance(double actual_l, double normative_l);
double sumDistance(const WaybillEntry& entry);
void recalculateEntry(WaybillEntry& entry, const Route& route);

}  // namespace waysheet::fuel
