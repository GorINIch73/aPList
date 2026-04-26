#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "models.hpp"

struct sqlite3;

namespace waysheet {

class Database {
 public:
  Database() = default;
  ~Database();

  bool open(const std::filesystem::path& dbPath);
  void close();
  bool isOpen() const;
  std::string currentPath() const;

  bool createSchema();
  bool addCar(const Car& car);
  bool addDriver(const Driver& driver);
  bool addRoute(const Route& route);
  bool addWaybill(const WaybillEntry& entry);
  bool upsertCarRouteNorm(const CarRouteNorm& norm);
  bool updateWaybill(const WaybillEntry& entry);
  bool updateCar(const Car& car);
  bool updateDriver(const Driver& driver);
  bool updateRoute(const Route& route);
  bool deleteCar(std::int64_t id);
  bool deleteDriver(std::int64_t id);
  bool deleteRoute(std::int64_t id);

  std::vector<Car> listCars() const;
  std::vector<Driver> listDrivers() const;
  std::vector<Route> listRoutes() const;
  std::vector<CarRouteNorm> listCarRouteNorms(std::int64_t carId) const;
  std::vector<WaybillEntry> listWaybills() const;
  bool deleteWaybill(std::int64_t id);
  std::int64_t countRows(const std::string& table) const;
  std::string lastError() const;

 private:
  bool exec(const std::string& sql) const;
  bool loadStubData();
  bool saveStubData() const;

  sqlite3* db_{nullptr};
  std::string path_;
  mutable std::string last_error_;

  std::vector<Car> cars_mem_;
  std::vector<Driver> drivers_mem_;
  std::vector<Route> routes_mem_;
  std::vector<CarRouteNorm> car_route_norms_mem_;
  std::vector<WaybillEntry> waybills_mem_;
  std::int64_t next_car_id_{1};
  std::int64_t next_driver_id_{1};
  std::int64_t next_route_id_{1};
  std::int64_t next_car_route_norm_id_{1};
  std::int64_t next_waybill_id_{1};
  std::int64_t next_detail_id_{1};
};

}  // namespace waysheet
