#include "fuel_math.hpp"

namespace waysheet::fuel {

std::string detectSeason(int month) {
  return (month >= 4 && month <= 10) ? "summer" : "winter";
}

double calcNormativeFuel(double distance_km, double route_norm_l100) {
  return (distance_km * route_norm_l100) / 100.0;
}

double calcActualFuel(double fuel_start_l, double fuel_added_l, double fuel_end_l) {
  return fuel_start_l + fuel_added_l - fuel_end_l;
}

double calcVariance(double actual_l, double normative_l) {
  return actual_l - normative_l;
}

double sumDistance(const WaybillEntry& entry) {
  double total = 0.0;
  for (const auto& d : entry.details) {
    total += d.distance_km;
  }
  return total;
}

void recalculateEntry(WaybillEntry& entry, const Route& route) {
  entry.calculated_fuel_l = 0.0;
  for (auto& d : entry.details) {
    const bool isSummer = d.season == "summer";
    const double norm = isSummer ? route.norm_summer_l100 : route.norm_winter_l100;
    d.calculated_fuel_l = calcNormativeFuel(d.distance_km, norm);
    entry.calculated_fuel_l += d.calculated_fuel_l;
  }

  entry.odometer_end = entry.odometer_start + sumDistance(entry);
  entry.fuel_end_l = entry.fuel_start_l + entry.fuel_added_l - entry.calculated_fuel_l;
  entry.actual_fuel_l = entry.calculated_fuel_l;
  entry.variance_l = calcVariance(entry.actual_fuel_l, entry.calculated_fuel_l);
}

}  // namespace waysheet::fuel
