#include "database.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

#if WAYSHEET_HAS_SQLITE
#include <sqlite3.h>
#endif

namespace waysheet {

namespace {

bool TryParseDouble(const std::string& text, double& value) {
  try {
    std::size_t pos = 0;
    value = std::stod(text, &pos);
    return pos == text.size();
  } catch (...) {
    return false;
  }
}

}  // namespace

Database::~Database() { close(); }

bool Database::open(const std::filesystem::path& dbPath) {
  close();
#if WAYSHEET_HAS_SQLITE
  if (sqlite3_open(dbPath.string().c_str(), &db_) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    std::cerr << "sqlite open failed: " << last_error_ << "\n";
    close();
    return false;
  }
  exec("PRAGMA foreign_keys = ON;");
  path_ = dbPath.string();
  return true;
#else
  path_ = dbPath.string();
  return loadStubData();
#endif
}

void Database::close() {
#if WAYSHEET_HAS_SQLITE
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
#endif
#if !WAYSHEET_HAS_SQLITE
  if (!path_.empty()) {
    saveStubData();
  }
  cars_mem_.clear();
  drivers_mem_.clear();
  routes_mem_.clear();
  car_route_norms_mem_.clear();
  waybills_mem_.clear();
  next_car_id_ = 1;
  next_driver_id_ = 1;
  next_route_id_ = 1;
  next_car_route_norm_id_ = 1;
  next_waybill_id_ = 1;
  next_detail_id_ = 1;
#endif
  path_.clear();
  last_error_.clear();
}

bool Database::isOpen() const {
#if WAYSHEET_HAS_SQLITE
  return db_ != nullptr;
#else
  return !path_.empty();
#endif
}

std::string Database::currentPath() const { return path_; }

bool Database::createSchema() {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* ddl = R"sql(
CREATE TABLE IF NOT EXISTS cars (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  plate_number TEXT NOT NULL,
  brand TEXT NOT NULL,
  tank_volume_l REAL NOT NULL,
  initial_odometer REAL NOT NULL DEFAULT 0,
  initial_fuel_l REAL NOT NULL DEFAULT 0,
  registration_date TEXT NOT NULL,
  status TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS routes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  norm_summer_l100 REAL NOT NULL,
  norm_winter_l100 REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS car_route_norms (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  car_id INTEGER NOT NULL REFERENCES cars(id) ON DELETE CASCADE,
  route_id INTEGER NOT NULL REFERENCES routes(id) ON DELETE CASCADE,
  norm_summer_l100 REAL NOT NULL,
  norm_winter_l100 REAL NOT NULL,
  UNIQUE(car_id, route_id)
);
CREATE TABLE IF NOT EXISTS drivers (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  full_name TEXT NOT NULL,
  phone TEXT,
  license_no TEXT,
  status TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS waybill_entries (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  car_id INTEGER NOT NULL REFERENCES cars(id),
  driver_id INTEGER NOT NULL REFERENCES drivers(id),
  date TEXT NOT NULL,
  odometer_start REAL NOT NULL,
  fuel_start_l REAL NOT NULL,
  fuel_added_l REAL NOT NULL,
  odometer_end REAL NOT NULL,
  fuel_end_l REAL NOT NULL,
  calculated_fuel_l REAL NOT NULL,
  actual_fuel_l REAL NOT NULL,
  variance_l REAL NOT NULL,
  notes TEXT
);
CREATE TABLE IF NOT EXISTS waybill_route_details (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  waybill_entry_id INTEGER NOT NULL REFERENCES waybill_entries(id) ON DELETE CASCADE,
  route_id INTEGER NOT NULL REFERENCES routes(id),
  distance_km REAL NOT NULL,
  season TEXT NOT NULL,
  calculated_fuel_l REAL NOT NULL
);
)sql";

  if (!exec(ddl)) return false;
  auto hasColumn = [&](const char* table, const char* column) {
    std::string sql = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      if (name && column == std::string(name)) {
        found = true;
        break;
      }
    }
    sqlite3_finalize(stmt);
    return found;
  };
  if (!hasColumn("cars", "initial_odometer") &&
      !exec("ALTER TABLE cars ADD COLUMN initial_odometer REAL NOT NULL DEFAULT 0;")) {
    return false;
  }
  if (!hasColumn("cars", "initial_fuel_l") && !exec("ALTER TABLE cars ADD COLUMN initial_fuel_l REAL NOT NULL DEFAULT 0;")) {
    return false;
  }
#endif
  return true;
}

bool Database::addCar(const Car& car) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql =
      "INSERT INTO cars (plate_number, brand, tank_volume_l, initial_odometer, initial_fuel_l, registration_date, status) "
      "VALUES (?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, car.plate_number.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, car.brand.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 3, car.tank_volume_l);
  sqlite3_bind_double(stmt, 4, car.initial_odometer);
  sqlite3_bind_double(stmt, 5, car.initial_fuel_l);
  sqlite3_bind_text(stmt, 6, car.registration_date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, car.status.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  if (!path_.empty()) {
    saveStubData();
  }
  (void)car;
  Car copy = car;
  copy.id = next_car_id_++;
  cars_mem_.push_back(copy);
  return saveStubData();
#endif
}

bool Database::addDriver(const Driver& driver) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "INSERT INTO drivers (full_name, phone, license_no, status) VALUES (?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, driver.full_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, driver.phone.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, driver.license_no.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, driver.status.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  Driver copy = driver;
  copy.id = next_driver_id_++;
  drivers_mem_.push_back(copy);
  return saveStubData();
#endif
}

bool Database::addRoute(const Route& route) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "INSERT INTO routes (name, norm_summer_l100, norm_winter_l100) VALUES (?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, route.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 2, route.norm_summer_l100);
  sqlite3_bind_double(stmt, 3, route.norm_winter_l100);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  Route copy = route;
  copy.id = next_route_id_++;
  routes_mem_.push_back(copy);
  return saveStubData();
#endif
}

bool Database::upsertCarRouteNorm(const CarRouteNorm& norm) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql =
      "INSERT INTO car_route_norms (car_id, route_id, norm_summer_l100, norm_winter_l100) VALUES (?, ?, ?, ?) "
      "ON CONFLICT(car_id, route_id) DO UPDATE SET norm_summer_l100 = excluded.norm_summer_l100, "
      "norm_winter_l100 = excluded.norm_winter_l100;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_int64(stmt, 1, norm.car_id);
  sqlite3_bind_int64(stmt, 2, norm.route_id);
  sqlite3_bind_double(stmt, 3, norm.norm_summer_l100);
  sqlite3_bind_double(stmt, 4, norm.norm_winter_l100);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  for (auto& existing : car_route_norms_mem_) {
    if (existing.car_id != norm.car_id || existing.route_id != norm.route_id) continue;
    existing.norm_summer_l100 = norm.norm_summer_l100;
    existing.norm_winter_l100 = norm.norm_winter_l100;
    return saveStubData();
  }
  CarRouteNorm copy = norm;
  copy.id = next_car_route_norm_id_++;
  car_route_norms_mem_.push_back(copy);
  return saveStubData();
#endif
}

bool Database::addWaybill(const WaybillEntry& entry) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  if (!exec("BEGIN TRANSACTION;")) {
    return false;
  }
  const char* mainSql =
      "INSERT INTO waybill_entries (car_id, driver_id, date, odometer_start, fuel_start_l, fuel_added_l, "
      "odometer_end, fuel_end_l, calculated_fuel_l, actual_fuel_l, variance_l, notes) VALUES (?, ?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?, ?);";
  sqlite3_stmt* mainStmt = nullptr;
  if (sqlite3_prepare_v2(db_, mainSql, -1, &mainStmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_bind_int64(mainStmt, 1, entry.car_id);
  sqlite3_bind_int64(mainStmt, 2, entry.driver_id);
  sqlite3_bind_text(mainStmt, 3, entry.date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(mainStmt, 4, entry.odometer_start);
  sqlite3_bind_double(mainStmt, 5, entry.fuel_start_l);
  sqlite3_bind_double(mainStmt, 6, entry.fuel_added_l);
  sqlite3_bind_double(mainStmt, 7, entry.odometer_end);
  sqlite3_bind_double(mainStmt, 8, entry.fuel_end_l);
  sqlite3_bind_double(mainStmt, 9, entry.calculated_fuel_l);
  sqlite3_bind_double(mainStmt, 10, entry.actual_fuel_l);
  sqlite3_bind_double(mainStmt, 11, entry.variance_l);
  sqlite3_bind_text(mainStmt, 12, entry.notes.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(mainStmt) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(mainStmt);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_finalize(mainStmt);

  const std::int64_t waybillId = sqlite3_last_insert_rowid(db_);
  const char* detailSql =
      "INSERT INTO waybill_route_details (waybill_entry_id, route_id, distance_km, season, calculated_fuel_l) VALUES "
      "(?, ?, ?, ?, ?);";
  sqlite3_stmt* detailStmt = nullptr;
  if (sqlite3_prepare_v2(db_, detailSql, -1, &detailStmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK;");
    return false;
  }
  for (const auto& d : entry.details) {
    sqlite3_reset(detailStmt);
    sqlite3_clear_bindings(detailStmt);
    sqlite3_bind_int64(detailStmt, 1, waybillId);
    sqlite3_bind_int64(detailStmt, 2, d.route_id);
    sqlite3_bind_double(detailStmt, 3, d.distance_km);
    sqlite3_bind_text(detailStmt, 4, d.season.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(detailStmt, 5, d.calculated_fuel_l);
    if (sqlite3_step(detailStmt) != SQLITE_DONE) {
      last_error_ = sqlite3_errmsg(db_);
      sqlite3_finalize(detailStmt);
      exec("ROLLBACK;");
      return false;
    }
  }
  sqlite3_finalize(detailStmt);
  return exec("COMMIT;");
#else
  WaybillEntry copy = entry;
  copy.id = next_waybill_id_++;
  for (auto& d : copy.details) {
    d.id = next_detail_id_++;
    d.waybill_entry_id = copy.id;
  }
  waybills_mem_.push_back(copy);
  return saveStubData();
#endif
}

bool Database::updateWaybill(const WaybillEntry& entry) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  if (!exec("BEGIN TRANSACTION;")) return false;

  const char* updSql =
      "UPDATE waybill_entries SET car_id = ?, driver_id = ?, date = ?, odometer_start = ?, fuel_start_l = ?, fuel_added_l = ?, "
      "odometer_end = ?, fuel_end_l = ?, calculated_fuel_l = ?, actual_fuel_l = ?, variance_l = ?, notes = ? WHERE id = ?;";
  sqlite3_stmt* updStmt = nullptr;
  if (sqlite3_prepare_v2(db_, updSql, -1, &updStmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_bind_int64(updStmt, 1, entry.car_id);
  sqlite3_bind_int64(updStmt, 2, entry.driver_id);
  sqlite3_bind_text(updStmt, 3, entry.date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(updStmt, 4, entry.odometer_start);
  sqlite3_bind_double(updStmt, 5, entry.fuel_start_l);
  sqlite3_bind_double(updStmt, 6, entry.fuel_added_l);
  sqlite3_bind_double(updStmt, 7, entry.odometer_end);
  sqlite3_bind_double(updStmt, 8, entry.fuel_end_l);
  sqlite3_bind_double(updStmt, 9, entry.calculated_fuel_l);
  sqlite3_bind_double(updStmt, 10, entry.actual_fuel_l);
  sqlite3_bind_double(updStmt, 11, entry.variance_l);
  sqlite3_bind_text(updStmt, 12, entry.notes.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(updStmt, 13, entry.id);
  if (sqlite3_step(updStmt) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(updStmt);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_finalize(updStmt);

  const char* delSql = "DELETE FROM waybill_route_details WHERE waybill_entry_id = ?;";
  sqlite3_stmt* delStmt = nullptr;
  if (sqlite3_prepare_v2(db_, delSql, -1, &delStmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_bind_int64(delStmt, 1, entry.id);
  if (sqlite3_step(delStmt) != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(delStmt);
    exec("ROLLBACK;");
    return false;
  }
  sqlite3_finalize(delStmt);

  const char* insSql =
      "INSERT INTO waybill_route_details (waybill_entry_id, route_id, distance_km, season, calculated_fuel_l) VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt* insStmt = nullptr;
  if (sqlite3_prepare_v2(db_, insSql, -1, &insStmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK;");
    return false;
  }
  for (const auto& d : entry.details) {
    sqlite3_reset(insStmt);
    sqlite3_clear_bindings(insStmt);
    sqlite3_bind_int64(insStmt, 1, entry.id);
    sqlite3_bind_int64(insStmt, 2, d.route_id);
    sqlite3_bind_double(insStmt, 3, d.distance_km);
    sqlite3_bind_text(insStmt, 4, d.season.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(insStmt, 5, d.calculated_fuel_l);
    if (sqlite3_step(insStmt) != SQLITE_DONE) {
      last_error_ = sqlite3_errmsg(db_);
      sqlite3_finalize(insStmt);
      exec("ROLLBACK;");
      return false;
    }
  }
  sqlite3_finalize(insStmt);
  return exec("COMMIT;");
#else
  for (auto& w : waybills_mem_) {
    if (w.id != entry.id) continue;
    WaybillEntry copy = entry;
    for (auto& d : copy.details) {
      if (d.id == 0) d.id = next_detail_id_++;
      d.waybill_entry_id = copy.id;
    }
    w = std::move(copy);
    return saveStubData();
  }
  last_error_ = "Waybill not found";
  return false;
#endif
}

bool Database::updateCar(const Car& car) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql =
      "UPDATE cars SET plate_number = ?, brand = ?, tank_volume_l = ?, initial_odometer = ?, initial_fuel_l = ?, "
      "registration_date = ?, status = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, car.plate_number.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, car.brand.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 3, car.tank_volume_l);
  sqlite3_bind_double(stmt, 4, car.initial_odometer);
  sqlite3_bind_double(stmt, 5, car.initial_fuel_l);
  sqlite3_bind_text(stmt, 6, car.registration_date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, car.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 8, car.id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  for (auto& c : cars_mem_) {
    if (c.id != car.id) continue;
    c = car;
    return saveStubData();
  }
  last_error_ = "Car not found";
  return false;
#endif
}

bool Database::updateDriver(const Driver& driver) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "UPDATE drivers SET full_name = ?, phone = ?, license_no = ?, status = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, driver.full_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, driver.phone.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, driver.license_no.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, driver.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, driver.id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  for (auto& d : drivers_mem_) {
    if (d.id != driver.id) continue;
    d = driver;
    return saveStubData();
  }
  last_error_ = "Driver not found";
  return false;
#endif
}

bool Database::updateRoute(const Route& route) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "UPDATE routes SET name = ?, norm_summer_l100 = ?, norm_winter_l100 = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_text(stmt, 1, route.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 2, route.norm_summer_l100);
  sqlite3_bind_double(stmt, 3, route.norm_winter_l100);
  sqlite3_bind_int64(stmt, 4, route.id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  for (auto& r : routes_mem_) {
    if (r.id != route.id) continue;
    r = route;
    return saveStubData();
  }
  last_error_ = "Route not found";
  return false;
#endif
}

bool Database::deleteCar(std::int64_t id) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "DELETE FROM cars WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_int64(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  cars_mem_.erase(std::remove_if(cars_mem_.begin(), cars_mem_.end(), [id](const Car& c) { return c.id == id; }),
                  cars_mem_.end());
  return saveStubData();
#endif
}

bool Database::deleteDriver(std::int64_t id) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "DELETE FROM drivers WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_int64(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  drivers_mem_.erase(
      std::remove_if(drivers_mem_.begin(), drivers_mem_.end(), [id](const Driver& d) { return d.id == id; }),
      drivers_mem_.end());
  return saveStubData();
#endif
}

bool Database::deleteRoute(std::int64_t id) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "DELETE FROM routes WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_int64(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  routes_mem_.erase(std::remove_if(routes_mem_.begin(), routes_mem_.end(), [id](const Route& r) { return r.id == id; }),
                    routes_mem_.end());
  return saveStubData();
#endif
}

bool Database::deleteWaybill(std::int64_t id) {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  const char* sql = "DELETE FROM waybill_entries WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  sqlite3_bind_int64(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  if (!ok) last_error_ = sqlite3_errmsg(db_);
  sqlite3_finalize(stmt);
  return ok;
#else
  waybills_mem_.erase(
      std::remove_if(waybills_mem_.begin(), waybills_mem_.end(), [id](const WaybillEntry& w) { return w.id == id; }),
      waybills_mem_.end());
  return saveStubData();
#endif
}

std::vector<Car> Database::listCars() const {
  std::vector<Car> rows;
#if WAYSHEET_HAS_SQLITE
  if (!db_) return rows;
  const char* sql =
      "SELECT id, plate_number, brand, tank_volume_l, initial_odometer, initial_fuel_l, registration_date, status "
      "FROM cars ORDER BY id DESC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return rows;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Car c{};
    c.id = sqlite3_column_int64(stmt, 0);
    c.plate_number = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    c.brand = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    c.tank_volume_l = sqlite3_column_double(stmt, 3);
    c.initial_odometer = sqlite3_column_double(stmt, 4);
    c.initial_fuel_l = sqlite3_column_double(stmt, 5);
    c.registration_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    c.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    rows.push_back(c);
  }
  sqlite3_finalize(stmt);
#endif
#if !WAYSHEET_HAS_SQLITE
  rows = cars_mem_;
  std::sort(rows.begin(), rows.end(), [](const Car& a, const Car& b) { return a.id > b.id; });
#endif
  return rows;
}

std::vector<Driver> Database::listDrivers() const {
  std::vector<Driver> rows;
#if WAYSHEET_HAS_SQLITE
  if (!db_) return rows;
  const char* sql = "SELECT id, full_name, phone, license_no, status FROM drivers ORDER BY id DESC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return rows;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Driver d{};
    d.id = sqlite3_column_int64(stmt, 0);
    d.full_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    d.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    d.license_no = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    d.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    rows.push_back(d);
  }
  sqlite3_finalize(stmt);
#endif
#if !WAYSHEET_HAS_SQLITE
  rows = drivers_mem_;
  std::sort(rows.begin(), rows.end(), [](const Driver& a, const Driver& b) { return a.id > b.id; });
#endif
  return rows;
}

std::vector<Route> Database::listRoutes() const {
  std::vector<Route> rows;
#if WAYSHEET_HAS_SQLITE
  if (!db_) return rows;
  const char* sql = "SELECT id, name, norm_summer_l100, norm_winter_l100 FROM routes ORDER BY id DESC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return rows;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Route r{};
    r.id = sqlite3_column_int64(stmt, 0);
    r.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    r.norm_summer_l100 = sqlite3_column_double(stmt, 2);
    r.norm_winter_l100 = sqlite3_column_double(stmt, 3);
    rows.push_back(r);
  }
  sqlite3_finalize(stmt);
#endif
#if !WAYSHEET_HAS_SQLITE
  rows = routes_mem_;
  std::sort(rows.begin(), rows.end(), [](const Route& a, const Route& b) { return a.id > b.id; });
#endif
  return rows;
}

std::vector<CarRouteNorm> Database::listCarRouteNorms(std::int64_t carId) const {
  std::vector<CarRouteNorm> rows;
#if WAYSHEET_HAS_SQLITE
  if (!db_) return rows;
  const char* sql =
      "SELECT id, car_id, route_id, norm_summer_l100, norm_winter_l100 FROM car_route_norms WHERE car_id = ? ORDER BY route_id ASC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return rows;
  }
  sqlite3_bind_int64(stmt, 1, carId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    CarRouteNorm n{};
    n.id = sqlite3_column_int64(stmt, 0);
    n.car_id = sqlite3_column_int64(stmt, 1);
    n.route_id = sqlite3_column_int64(stmt, 2);
    n.norm_summer_l100 = sqlite3_column_double(stmt, 3);
    n.norm_winter_l100 = sqlite3_column_double(stmt, 4);
    rows.push_back(n);
  }
  sqlite3_finalize(stmt);
#endif
#if !WAYSHEET_HAS_SQLITE
  for (const auto& n : car_route_norms_mem_) {
    if (n.car_id == carId) rows.push_back(n);
  }
  std::sort(rows.begin(), rows.end(), [](const CarRouteNorm& a, const CarRouteNorm& b) { return a.route_id < b.route_id; });
#endif
  return rows;
}

std::vector<WaybillEntry> Database::listWaybills() const {
  std::vector<WaybillEntry> rows;
#if WAYSHEET_HAS_SQLITE
  if (!db_) return rows;
  const char* sql =
      "SELECT id, car_id, driver_id, date, odometer_start, fuel_start_l, fuel_added_l, odometer_end, fuel_end_l, "
      "calculated_fuel_l, actual_fuel_l, variance_l, notes FROM waybill_entries ORDER BY date DESC, id DESC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return rows;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    WaybillEntry w{};
    w.id = sqlite3_column_int64(stmt, 0);
    w.car_id = sqlite3_column_int64(stmt, 1);
    w.driver_id = sqlite3_column_int64(stmt, 2);
    w.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    w.odometer_start = sqlite3_column_double(stmt, 4);
    w.fuel_start_l = sqlite3_column_double(stmt, 5);
    w.fuel_added_l = sqlite3_column_double(stmt, 6);
    w.odometer_end = sqlite3_column_double(stmt, 7);
    w.fuel_end_l = sqlite3_column_double(stmt, 8);
    w.calculated_fuel_l = sqlite3_column_double(stmt, 9);
    w.actual_fuel_l = sqlite3_column_double(stmt, 10);
    w.variance_l = sqlite3_column_double(stmt, 11);
    w.notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    rows.push_back(w);
  }
  sqlite3_finalize(stmt);

  if (!rows.empty()) {
    const char* dsql =
        "SELECT id, waybill_entry_id, route_id, distance_km, season, calculated_fuel_l FROM waybill_route_details "
        "ORDER BY id ASC;";
    sqlite3_stmt* dstmt = nullptr;
    if (sqlite3_prepare_v2(db_, dsql, -1, &dstmt, nullptr) == SQLITE_OK) {
      std::unordered_map<std::int64_t, std::size_t> idx;
      for (std::size_t i = 0; i < rows.size(); ++i) idx.emplace(rows[i].id, i);
      while (sqlite3_step(dstmt) == SQLITE_ROW) {
        WaybillRouteDetail d{};
        d.id = sqlite3_column_int64(dstmt, 0);
        d.waybill_entry_id = sqlite3_column_int64(dstmt, 1);
        d.route_id = sqlite3_column_int64(dstmt, 2);
        d.distance_km = sqlite3_column_double(dstmt, 3);
        d.season = reinterpret_cast<const char*>(sqlite3_column_text(dstmt, 4));
        d.calculated_fuel_l = sqlite3_column_double(dstmt, 5);
        auto it = idx.find(d.waybill_entry_id);
        if (it != idx.end()) rows[it->second].details.push_back(d);
      }
      sqlite3_finalize(dstmt);
    }
  }
#else
  rows = waybills_mem_;
  std::sort(rows.begin(), rows.end(), [](const WaybillEntry& a, const WaybillEntry& b) {
    if (a.date != b.date) return a.date > b.date;
    return a.id > b.id;
  });
#endif
  return rows;
}

std::int64_t Database::countRows(const std::string& table) const {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return 0;
  const std::string sql = "SELECT COUNT(*) FROM " + table + ";";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    return 0;
  }
  std::int64_t count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
#else
  if (table == "cars") return static_cast<std::int64_t>(cars_mem_.size());
  if (table == "drivers") return static_cast<std::int64_t>(drivers_mem_.size());
  if (table == "routes") return static_cast<std::int64_t>(routes_mem_.size());
  if (table == "waybill_entries") return static_cast<std::int64_t>(waybills_mem_.size());
  if (table == "waybill_route_details") {
    std::int64_t n = 0;
    for (const auto& w : waybills_mem_) n += static_cast<std::int64_t>(w.details.size());
    return n;
  }
  return 0;
#endif
}

std::string Database::lastError() const {
  return last_error_;
}

bool Database::exec(const std::string& sql) const {
#if WAYSHEET_HAS_SQLITE
  if (!db_) return false;
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    last_error_ = (err ? err : sqlite3_errmsg(db_));
    std::cerr << "sqlite exec error: " << last_error_ << "\n";
    sqlite3_free(err);
    return false;
  }
  return true;
#else
  (void)sql;
  return true;
#endif
}

bool Database::loadStubData() {
#if WAYSHEET_HAS_SQLITE
  return true;
#else
  cars_mem_.clear();
  drivers_mem_.clear();
  routes_mem_.clear();
  waybills_mem_.clear();
  next_car_id_ = 1;
  next_driver_id_ = 1;
  next_route_id_ = 1;
  next_waybill_id_ = 1;
  next_detail_id_ = 1;

  if (path_.empty()) return false;
  if (!std::filesystem::exists(path_)) return true;

  std::ifstream in(path_);
  if (!in.is_open()) return false;

  std::unordered_map<std::int64_t, std::size_t> wbIndex;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream ss(line);
    std::string tag;
    std::getline(ss, tag, '\t');
    if (tag == "CAR") {
      Car c{};
      std::string t;
      std::getline(ss, t, '\t'); c.id = std::stoll(t);
      std::getline(ss, c.plate_number, '\t');
      std::getline(ss, c.brand, '\t');
      std::getline(ss, t, '\t'); c.tank_volume_l = std::stod(t);
      std::getline(ss, t, '\t');
      double parsed = 0.0;
      if (TryParseDouble(t, parsed)) {
        c.initial_odometer = parsed;
        std::getline(ss, t, '\t');
        c.initial_fuel_l = TryParseDouble(t, parsed) ? parsed : 0.0;
        std::getline(ss, c.registration_date, '\t');
        std::getline(ss, c.status, '\t');
      } else {
        c.registration_date = t;
        std::getline(ss, c.status, '\t');
      }
      cars_mem_.push_back(c);
      next_car_id_ = std::max(next_car_id_, c.id + 1);
    } else if (tag == "DRIVER") {
      Driver d{};
      std::string t;
      std::getline(ss, t, '\t'); d.id = std::stoll(t);
      std::getline(ss, d.full_name, '\t');
      std::getline(ss, d.phone, '\t');
      std::getline(ss, d.license_no, '\t');
      std::getline(ss, d.status, '\t');
      drivers_mem_.push_back(d);
      next_driver_id_ = std::max(next_driver_id_, d.id + 1);
    } else if (tag == "ROUTE") {
      Route r{};
      std::string t;
      std::getline(ss, t, '\t'); r.id = std::stoll(t);
      std::getline(ss, r.name, '\t');
      std::getline(ss, t, '\t'); r.norm_summer_l100 = std::stod(t);
      std::getline(ss, t, '\t'); r.norm_winter_l100 = std::stod(t);
      routes_mem_.push_back(r);
      next_route_id_ = std::max(next_route_id_, r.id + 1);
    } else if (tag == "CAR_ROUTE_NORM") {
      CarRouteNorm n{};
      std::string t;
      std::getline(ss, t, '\t'); n.id = std::stoll(t);
      std::getline(ss, t, '\t'); n.car_id = std::stoll(t);
      std::getline(ss, t, '\t'); n.route_id = std::stoll(t);
      std::getline(ss, t, '\t'); n.norm_summer_l100 = std::stod(t);
      std::getline(ss, t, '\t'); n.norm_winter_l100 = std::stod(t);
      car_route_norms_mem_.push_back(n);
      next_car_route_norm_id_ = std::max(next_car_route_norm_id_, n.id + 1);
    } else if (tag == "WAYBILL") {
      WaybillEntry w{};
      std::string t;
      std::getline(ss, t, '\t'); w.id = std::stoll(t);
      std::getline(ss, t, '\t'); w.car_id = std::stoll(t);
      std::getline(ss, t, '\t'); w.driver_id = std::stoll(t);
      std::getline(ss, w.date, '\t');
      std::getline(ss, t, '\t'); w.odometer_start = std::stod(t);
      std::getline(ss, t, '\t'); w.fuel_start_l = std::stod(t);
      std::getline(ss, t, '\t'); w.fuel_added_l = std::stod(t);
      std::getline(ss, t, '\t'); w.odometer_end = std::stod(t);
      std::getline(ss, t, '\t'); w.fuel_end_l = std::stod(t);
      std::getline(ss, t, '\t'); w.calculated_fuel_l = std::stod(t);
      std::getline(ss, t, '\t'); w.actual_fuel_l = std::stod(t);
      std::getline(ss, t, '\t'); w.variance_l = std::stod(t);
      std::getline(ss, w.notes, '\t');
      wbIndex[w.id] = waybills_mem_.size();
      waybills_mem_.push_back(w);
      next_waybill_id_ = std::max(next_waybill_id_, w.id + 1);
    } else if (tag == "DETAIL") {
      WaybillRouteDetail d{};
      std::string t;
      std::getline(ss, t, '\t'); d.id = std::stoll(t);
      std::getline(ss, t, '\t'); d.waybill_entry_id = std::stoll(t);
      std::getline(ss, t, '\t'); d.route_id = std::stoll(t);
      std::getline(ss, t, '\t'); d.distance_km = std::stod(t);
      std::getline(ss, d.season, '\t');
      std::getline(ss, t, '\t'); d.calculated_fuel_l = std::stod(t);
      auto it = wbIndex.find(d.waybill_entry_id);
      if (it != wbIndex.end()) waybills_mem_[it->second].details.push_back(d);
      next_detail_id_ = std::max(next_detail_id_, d.id + 1);
    }
  }
  return true;
#endif
}

bool Database::saveStubData() const {
#if WAYSHEET_HAS_SQLITE
  return true;
#else
  if (path_.empty()) return false;
  std::ofstream out(path_, std::ios::trunc);
  if (!out.is_open()) return false;
  out << "#WAYSHEET_STUB_V1\n";
  for (const auto& c : cars_mem_) {
    out << "CAR\t" << c.id << '\t' << c.plate_number << '\t' << c.brand << '\t' << c.tank_volume_l << '\t'
        << c.initial_odometer << '\t' << c.initial_fuel_l << '\t' << c.registration_date << '\t' << c.status << '\n';
  }
  for (const auto& d : drivers_mem_) {
    out << "DRIVER\t" << d.id << '\t' << d.full_name << '\t' << d.phone << '\t' << d.license_no << '\t'
        << d.status << '\n';
  }
  for (const auto& r : routes_mem_) {
    out << "ROUTE\t" << r.id << '\t' << r.name << '\t' << r.norm_summer_l100 << '\t' << r.norm_winter_l100 << '\n';
  }
  for (const auto& n : car_route_norms_mem_) {
    out << "CAR_ROUTE_NORM\t" << n.id << '\t' << n.car_id << '\t' << n.route_id << '\t' << n.norm_summer_l100
        << '\t' << n.norm_winter_l100 << '\n';
  }
  for (const auto& w : waybills_mem_) {
    out << "WAYBILL\t" << w.id << '\t' << w.car_id << '\t' << w.driver_id << '\t' << w.date << '\t'
        << w.odometer_start << '\t' << w.fuel_start_l << '\t' << w.fuel_added_l << '\t' << w.odometer_end << '\t'
        << w.fuel_end_l << '\t' << w.calculated_fuel_l << '\t' << w.actual_fuel_l << '\t' << w.variance_l << '\t'
        << w.notes << '\n';
    for (const auto& dt : w.details) {
      out << "DETAIL\t" << dt.id << '\t' << dt.waybill_entry_id << '\t' << dt.route_id << '\t' << dt.distance_km
          << '\t' << dt.season << '\t' << dt.calculated_fuel_l << '\n';
    }
  }
  return true;
#endif
}

}  // namespace waysheet
