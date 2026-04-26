#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace waysheet {

struct Car {
  std::int64_t id{};
  std::string plate_number;
  std::string brand;
  double tank_volume_l{};
  double initial_odometer{};
  double initial_fuel_l{};
  std::string registration_date;
  std::string status = "active";
};

struct Route {
  std::int64_t id{};
  std::string name;
  double norm_summer_l100{};
  double norm_winter_l100{};
};

struct CarRouteNorm {
  std::int64_t id{};
  std::int64_t car_id{};
  std::int64_t route_id{};
  double norm_summer_l100{};
  double norm_winter_l100{};
};

struct Driver {
  std::int64_t id{};
  std::string full_name;
  std::string phone;
  std::string license_no;
  std::string status = "active";
};

struct WaybillRouteDetail {
  std::int64_t id{};
  std::int64_t waybill_entry_id{};
  std::int64_t route_id{};
  double distance_km{};
  std::string season;
  double calculated_fuel_l{};
};

struct WaybillEntry {
  std::int64_t id{};
  std::int64_t car_id{};
  std::int64_t driver_id{};
  std::string date;
  double odometer_start{};
  double fuel_start_l{};
  double fuel_added_l{};
  double odometer_end{};
  double fuel_end_l{};
  double calculated_fuel_l{};
  double actual_fuel_l{};
  double variance_l{};
  std::string notes;
  std::vector<WaybillRouteDetail> details;
};

}  // namespace waysheet
