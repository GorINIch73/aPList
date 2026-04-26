#include "app.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <unordered_map>

#include "fuel_math.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#if WAYSHEET_ENABLE_IMGUI
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#endif

namespace waysheet {

#if WAYSHEET_ENABLE_IMGUI
namespace {

static const ImWchar ICON_MIN_FA = 0xf000;
static const ImWchar ICON_MAX_FA = 0xf3ff;
static const ImWchar ICON_RANGES_FA[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

inline const char* U8(const char8_t* s) { return reinterpret_cast<const char*>(s); }
const char* kIconDashboard = U8(u8"\uf0e8");
const char* kIconDb = U8(u8"\uf1c0");
const char* kIconCar = U8(u8"\uf1b9");
const char* kIconDriver = U8(u8"\uf007");
const char* kIconRoute = U8(u8"\uf061");
const char* kIconWaybill = U8(u8"\uf044");
const char* kIconSummary = U8(u8"\uf201");
const char* kIconAdd = U8(u8"\uf055");
const char* kIconDelete = U8(u8"\uf1f8");
const char* kIconOpen = U8(u8"\uf07b");
const char* kIconSave = U8(u8"\uf0c7");
const char* kIconClose = U8(u8"\uf011");

std::string gLoadedBaseFont;
std::string gLoadedIconFont;

std::string IconLabel(const char* icon, const char* text) {
  return std::string(icon) + " " + text;
}

bool IconTextButton(const char* icon, const char* text, const char* id, const ImVec2& size = ImVec2(0, 0)) {
  const std::string label = IconLabel(icon, text) + "##" + id;
  return ImGui::Button(label.c_str(), size);
}

std::string PathToUtf8String(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
}

std::string CsvEscape(std::string value) {
  bool needsQuotes = false;
  for (const char ch : value) {
    if (ch == '"' || ch == ';' || ch == '\n' || ch == '\r') {
      needsQuotes = true;
      break;
    }
  }
  if (!needsQuotes) return value;

  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const char ch : value) {
    if (ch == '"') escaped.push_back('"');
    escaped.push_back(ch);
  }
  escaped.push_back('"');
  return escaped;
}

std::string HtmlEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string FormatCsvNumber(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

void OpenFileWithDefaultApp(const std::filesystem::path& path) {
#ifdef _WIN32
  ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  const std::string command = "open " + ShellQuote(path.string()) + " >/dev/null 2>&1 &";
  std::system(command.c_str());
#else
  const std::string command = "xdg-open " + ShellQuote(path.string()) + " >/dev/null 2>&1 &";
  std::system(command.c_str());
#endif
}

struct OpeningBalance {
  double odometer{};
  double fuel_l{};
  bool fromPreviousWaybill{};
};

OpeningBalance FindOpeningBalance(const std::vector<Car>& cars, const std::vector<WaybillEntry>& waybills,
                                  std::int64_t carId, std::int64_t excludeWaybillId = 0) {
  OpeningBalance balance{};
  for (const auto& car : cars) {
    if (car.id != carId) continue;
    balance.odometer = car.initial_odometer;
    balance.fuel_l = car.initial_fuel_l;
    break;
  }

  const WaybillEntry* latest = nullptr;
  for (const auto& waybill : waybills) {
    if (waybill.car_id != carId || waybill.id == excludeWaybillId) continue;
    if (!latest || waybill.date > latest->date || (waybill.date == latest->date && waybill.id > latest->id)) {
      latest = &waybill;
    }
  }
  if (latest) {
    balance.odometer = latest->odometer_end;
    balance.fuel_l = latest->fuel_end_l;
    balance.fromPreviousWaybill = true;
  }
  return balance;
}

std::filesystem::path DefaultWaybillCsvPath(const std::string& dbPath) {
  std::filesystem::path dir = std::filesystem::current_path();
  std::string stem = "waybills";
  if (!dbPath.empty()) {
    const std::filesystem::path path(dbPath);
    if (!path.parent_path().empty()) dir = path.parent_path();
    if (!path.stem().empty()) stem = PathToUtf8String(path.stem());
  }
  return dir / (stem + "_waybills.csv");
}

bool ExportWaybillJournalCsv(const std::filesystem::path& path, const std::vector<WaybillEntry>& rows,
                             const std::unordered_map<std::int64_t, std::string>& carNameById,
                             const std::unordered_map<std::int64_t, std::string>& driverNameById,
                             std::int64_t filterCarId, std::int64_t filterDriverId,
                             const std::string& dateFrom, const std::string& dateTo, std::string& error) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Cannot open output file";
    return false;
  }

  out << "\xEF\xBB\xBF";
  out << "ID;Date;Car;Driver;Distance km;Odometer start;Odometer end;Fuel start l;Fuel added l;Fuel end l;"
         "Norm fuel l;Actual fuel l;Variance l;Notes\n";

  for (const auto& w : rows) {
    if (filterCarId != 0 && w.car_id != filterCarId) continue;
    if (filterDriverId != 0 && w.driver_id != filterDriverId) continue;
    if (!dateFrom.empty() && w.date < dateFrom) continue;
    if (!dateTo.empty() && w.date > dateTo) continue;

    double distance = 0.0;
    for (const auto& d : w.details) distance += d.distance_km;

    const auto carIt = carNameById.find(w.car_id);
    const auto driverIt = driverNameById.find(w.driver_id);
    out << w.id << ';' << CsvEscape(w.date) << ';'
        << CsvEscape(carIt != carNameById.end() ? carIt->second : "") << ';'
        << CsvEscape(driverIt != driverNameById.end() ? driverIt->second : "") << ';'
        << FormatCsvNumber(distance, 1) << ';' << FormatCsvNumber(w.odometer_start, 1) << ';'
        << FormatCsvNumber(w.odometer_end, 1) << ';' << FormatCsvNumber(w.fuel_start_l, 2) << ';'
        << FormatCsvNumber(w.fuel_added_l, 2) << ';' << FormatCsvNumber(w.fuel_end_l, 2) << ';'
        << FormatCsvNumber(w.calculated_fuel_l, 2) << ';' << FormatCsvNumber(w.actual_fuel_l, 2) << ';'
        << FormatCsvNumber(w.variance_l, 2) << ';' << CsvEscape(w.notes) << '\n';
  }

  if (!out) {
    error = "Cannot write output file";
    return false;
  }
  return true;
}

int ParseMonthFromDate(const char* date) {
  try {
    const std::string d = date ? date : "";
    if (d.size() >= 7) {
      return std::stoi(d.substr(5, 2));
    }
  } catch (...) {
  }
  return 1;
}

bool IsLeapYear(int year) {
  return (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
}

int DaysInMonth(int year, int month) {
  static const int kDaysByMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  if (month == 2) return IsLeapYear(year) ? 29 : 28;
  return kDaysByMonth[month - 1];
}

bool ParseIsoDate(const char* text, int& year, int& month, int& day) {
  year = 0;
  month = 0;
  day = 0;
  if (!text) return false;
  if (std::sscanf(text, "%d-%d-%d", &year, &month, &day) != 3) return false;
  if (year < 1900 || year > 3000) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > DaysInMonth(year, month)) return false;
  return true;
}

void FillTodayDate(int& year, int& month, int& day) {
  const std::time_t now = std::time(nullptr);
  std::tm localTm{};
#ifdef _WIN32
  localtime_s(&localTm, &now);
#else
  localtime_r(&now, &localTm);
#endif
  year = localTm.tm_year + 1900;
  month = localTm.tm_mon + 1;
  day = localTm.tm_mday;
}

int WeekdayMondayFirst(int year, int month, int day) {
  std::tm dateTm{};
  dateTm.tm_year = year - 1900;
  dateTm.tm_mon = month - 1;
  dateTm.tm_mday = day;
  dateTm.tm_hour = 12;
  if (std::mktime(&dateTm) == -1) return 0;
  return dateTm.tm_wday == 0 ? 6 : (dateTm.tm_wday - 1);
}

void FormatIsoDate(int year, int month, int day, char* out, std::size_t outSize) {
  if (!out || outSize == 0) return;
  std::snprintf(out, outSize, "%04d-%02d-%02d", year, month, day);
}

void LoadRussianAndIconFonts(ImGuiIO& io) {
  const ImWchar* cyrillicRanges = io.Fonts->GetGlyphRangesCyrillic();

  const std::filesystem::path localFont = std::filesystem::path("fonts") / "NotoSans-Regular.ttf";
  const std::filesystem::path localFontUp = std::filesystem::path("..") / "fonts" / "NotoSans-Regular.ttf";
  const std::filesystem::path localFontUp2 = std::filesystem::path("..") / ".." / "fonts" / "NotoSans-Regular.ttf";
  const std::filesystem::path windowsSegoe = "C:/Windows/Fonts/segoeui.ttf";
  const std::filesystem::path windowsArial = "C:/Windows/Fonts/arial.ttf";

  ImFont* baseFont = nullptr;
  for (const auto& path : {localFont, localFontUp, localFontUp2, windowsSegoe, windowsArial}) {
    if (!std::filesystem::exists(path)) continue;
    baseFont = io.Fonts->AddFontFromFileTTF(path.string().c_str(), 18.0f, nullptr, cyrillicRanges);
    if (baseFont) {
      gLoadedBaseFont = path.string();
      break;
    }
  }
  if (!baseFont) {
    baseFont = io.Fonts->AddFontDefault();
    gLoadedBaseFont = "<default>";
  }
  io.FontDefault = baseFont;

  ImFontConfig iconCfg{};
  iconCfg.MergeMode = true;
  iconCfg.PixelSnapH = true;
  iconCfg.GlyphMinAdvanceX = 18.0f;

  const std::filesystem::path localFa6 = std::filesystem::path("fonts") / "fa-solid-900.ttf";
  const std::filesystem::path localFa6Up = std::filesystem::path("..") / "fonts" / "fa-solid-900.ttf";
  const std::filesystem::path localFa6Up2 = std::filesystem::path("..") / ".." / "fonts" / "fa-solid-900.ttf";
  const std::filesystem::path localFa4 = std::filesystem::path("fonts") / "fontawesome-webfont.ttf";
  const std::filesystem::path localFa4Up = std::filesystem::path("..") / "fonts" / "fontawesome-webfont.ttf";
  const std::filesystem::path localFa4Up2 = std::filesystem::path("..") / ".." / "fonts" / "fontawesome-webfont.ttf";

  for (const auto& path : {localFa6, localFa6Up, localFa6Up2, localFa4, localFa4Up, localFa4Up2}) {
    if (!std::filesystem::exists(path)) continue;
    if (io.Fonts->AddFontFromFileTTF(path.string().c_str(), 18.0f, &iconCfg, ICON_RANGES_FA)) {
      gLoadedIconFont = path.string();
      break;
    }
  }
  if (gLoadedIconFont.empty()) {
    gLoadedIconFont = "<not loaded>";
  }
  io.Fonts->Build();
}

}  // namespace
#endif

App::App() : dbPath_("waysheet.db") {
  const std::string defaultDb = dbPath_.string();
  std::snprintf(dbCreatePath_.data(), dbCreatePath_.size(), "%s", defaultDb.c_str());
  std::snprintf(dbOpenPath_.data(), dbOpenPath_.size(), "%s", defaultDb.c_str());
  std::snprintf(dbSaveAsPath_.data(), dbSaveAsPath_.size(), "%s", defaultDb.c_str());

  const std::time_t now = std::time(nullptr);
  std::tm localTm{};
#ifdef _WIN32
  localtime_s(&localTm, &now);
#else
  localtime_r(&now, &localTm);
#endif
  std::snprintf(waybillForm_.date.data(), waybillForm_.date.size(), "%04d-%02d-%02d", localTm.tm_year + 1900,
                localTm.tm_mon + 1, localTm.tm_mday);
}

int App::run() {
  loadRecentDbs();
  if (!initStorage()) {
    std::cerr << "Failed to initialize storage\n";
    return 1;
  }
  seedDemoData();

#if WAYSHEET_ENABLE_IMGUI
  return runGui();
#else
  showDashboardText();
  std::cout << "Press Enter to exit...\n";
  std::cin.get();
  return 0;
#endif
}

bool App::initStorage() {
  if (!recentDbs_.empty()) {
    const std::filesystem::path lastDb = recentDbs_.front();
    if (std::filesystem::exists(lastDb)) {
      if (openDatabase(lastDb.string())) {
        return true;
      }
    }
  }
  return openDatabase(dbPath_.string());
}

void App::seedDemoData() {
  if (!db_.isOpen()) return;
  if (db_.countRows("cars") > 0 || db_.countRows("drivers") > 0 || db_.countRows("routes") > 0) return;
  db_.addCar(Car{0, "A123BC77", "GAZelle NEXT", 80.0, 12500.0, 35.0, "2025-01-10", "active"});
  db_.addDriver(Driver{0, "Ivan Petrov", "+7-900-000-0000", "77 10 123456", "active"});
  db_.addRoute(Route{0, "Warehouse - City Center", 14.5, 16.2});
}

void App::showDashboardText() const {
  std::cout << "Waysheet dashboard\n";
  std::cout << "- DB: " << db_.currentPath() << "\n";
  std::cout << "- Current theme: " << themes_.current().name << "\n";
}

void App::pushRecentDb(const std::string& path) {
  if (path.empty()) return;
  recentDbs_.erase(std::remove(recentDbs_.begin(), recentDbs_.end(), path), recentDbs_.end());
  recentDbs_.insert(recentDbs_.begin(), path);
  if (recentDbs_.size() > 10) recentDbs_.resize(10);
  saveRecentDbs();
}

void App::loadRecentDbs() {
  recentDbs_.clear();
  std::ifstream in(recentDbStore_);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) recentDbs_.push_back(line);
    if (recentDbs_.size() >= 10) break;
  }
}

void App::saveRecentDbs() const {
  std::ofstream out(recentDbStore_, std::ios::trunc);
  for (const auto& path : recentDbs_) out << path << '\n';
}

bool App::createNewDatabase(const std::string& path) {
  if (path.empty()) {
    uiStatus_ = "Ошибка создания БД: пустой путь";
    return false;
  }
  const std::filesystem::path p(path);
  if (!p.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  if (!db_.open(p) || !db_.createSchema()) {
    uiStatus_ = "Ошибка создания БД: " + db_.lastError();
    return false;
  }
  dbPath_ = p;
  pushRecentDb(dbPath_.string());
  uiStatus_ = "База создана: " + dbPath_.string();
  return true;
}

bool App::openDatabase(const std::string& path) {
  if (path.empty()) {
    uiStatus_ = "Ошибка открытия БД: пустой путь";
    return false;
  }
  if (!db_.open(path) || !db_.createSchema()) {
    uiStatus_ = "Ошибка открытия БД: " + db_.lastError();
    return false;
  }
  dbPath_ = path;
  pushRecentDb(dbPath_.string());
  uiStatus_ = "База открыта: " + dbPath_.string();
  return true;
}

bool App::saveDatabaseAs(const std::string& path) {
  if (!db_.isOpen()) {
    uiStatus_ = "Ошибка \"Сохранить как\": база не открыта";
    return false;
  }
  if (path.empty()) {
    uiStatus_ = "Ошибка \"Сохранить как\": пустой путь";
    return false;
  }
  const auto source = std::filesystem::path(db_.currentPath());
  const auto target = std::filesystem::path(path);
  if (!target.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
  }
  db_.close();
  std::error_code ec;
  std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    openDatabase(source.string());
    uiStatus_ = "Ошибка \"Сохранить как\": " + ec.message();
    return false;
  }
  return openDatabase(target.string());
}

bool App::recalculateWaybillChainForCar(std::int64_t carId) {
  if (carId == 0) return true;

  const auto cars = db_.listCars();
  const auto carIt = std::find_if(cars.begin(), cars.end(), [carId](const Car& car) { return car.id == carId; });
  if (carIt == cars.end()) return true;

  const auto routes = db_.listRoutes();
  std::unordered_map<std::int64_t, Route> routeById;
  for (const auto& route : routes) routeById.emplace(route.id, route);

  std::unordered_map<std::int64_t, CarRouteNorm> normByRouteId;
  for (const auto& norm : db_.listCarRouteNorms(carId)) normByRouteId.emplace(norm.route_id, norm);

  std::vector<WaybillEntry> chain;
  for (const auto& waybill : db_.listWaybills()) {
    if (waybill.car_id == carId) chain.push_back(waybill);
  }
  std::sort(chain.begin(), chain.end(), [](const WaybillEntry& a, const WaybillEntry& b) {
    if (a.date != b.date) return a.date < b.date;
    return a.id < b.id;
  });

  double odometer = carIt->initial_odometer;
  double fuelLeft = carIt->initial_fuel_l;
  for (auto& waybill : chain) {
    waybill.odometer_start = odometer;
    waybill.fuel_start_l = fuelLeft;

    double totalDistance = 0.0;
    double totalFuel = 0.0;
    for (auto& detail : waybill.details) {
      const auto routeIt = routeById.find(detail.route_id);
      if (routeIt == routeById.end()) {
        detail.calculated_fuel_l = 0.0;
        continue;
      }

      const auto normIt = normByRouteId.find(detail.route_id);
      const double routeNorm =
          normIt != normByRouteId.end()
              ? (detail.season == "summer" ? normIt->second.norm_summer_l100 : normIt->second.norm_winter_l100)
              : (detail.season == "summer" ? routeIt->second.norm_summer_l100 : routeIt->second.norm_winter_l100);
      detail.calculated_fuel_l = fuel::calcNormativeFuel(detail.distance_km, routeNorm);
      totalDistance += detail.distance_km;
      totalFuel += detail.calculated_fuel_l;
    }

    waybill.odometer_end = waybill.odometer_start + totalDistance;
    waybill.calculated_fuel_l = totalFuel;
    waybill.fuel_end_l = waybill.fuel_start_l + waybill.fuel_added_l - waybill.calculated_fuel_l;
    waybill.actual_fuel_l = waybill.calculated_fuel_l;
    waybill.variance_l = fuel::calcVariance(waybill.actual_fuel_l, waybill.calculated_fuel_l);

    if (!db_.updateWaybill(waybill)) return false;
    odometer = waybill.odometer_end;
    fuelLeft = waybill.fuel_end_l;
  }

  return true;
}

bool App::exportMonthlyWaybillReportHtml(const std::string& month) {
  if (month.size() < 7) {
    uiStatus_ = "Ошибка отчета: выберите месяц";
    return false;
  }

  const auto cars = db_.listCars();
  const auto drivers = db_.listDrivers();
  const auto waybills = db_.listWaybills();
  std::unordered_map<std::int64_t, std::string> carNameById;
  std::unordered_map<std::int64_t, std::string> driverNameById;
  for (const auto& car : cars) carNameById.emplace(car.id, car.plate_number + " / " + car.brand);
  for (const auto& driver : drivers) driverNameById.emplace(driver.id, driver.full_name);

  struct ReportRow {
    WaybillEntry waybill;
    double distance{};
  };
  std::vector<ReportRow> rows;
  for (const auto& waybill : waybills) {
    if (waybill.date.rfind(month, 0) != 0) continue;
    ReportRow row{};
    row.waybill = waybill;
    for (const auto& detail : waybill.details) row.distance += detail.distance_km;
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end(), [](const ReportRow& a, const ReportRow& b) {
    if (a.waybill.date != b.waybill.date) return a.waybill.date < b.waybill.date;
    return a.waybill.id < b.waybill.id;
  });

  std::filesystem::path dir = std::filesystem::current_path();
  if (!db_.currentPath().empty()) {
    const std::filesystem::path dbPath(db_.currentPath());
    if (!dbPath.parent_path().empty()) dir = dbPath.parent_path();
  }
  const auto outPath = dir / ("waybill_report_" + month + ".html");

  std::ofstream out(outPath, std::ios::binary);
  if (!out) {
    uiStatus_ = "Ошибка отчета: не удалось создать файл";
    return false;
  }

  double totalDistance = 0.0;
  double totalFuel = 0.0;
  double totalAdded = 0.0;
  for (const auto& row : rows) {
    totalDistance += row.distance;
    totalFuel += row.waybill.calculated_fuel_l;
    totalAdded += row.waybill.fuel_added_l;
  }

  out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Отчет путевых листов " << HtmlEscape(month)
      << "</title><style>"
      << "@page{size:A4 landscape;margin:10mm}"
      << "body{font-family:Arial,'Noto Sans',sans-serif;color:#111;margin:0;font-size:10px}"
      << "h1{font-size:16px;margin:0 0 6px 0} .meta{margin-bottom:8px}"
      << "table{width:100%;border-collapse:collapse;table-layout:fixed}"
      << "th,td{border:1px solid #333;padding:3px 4px;vertical-align:top;overflow:hidden;text-overflow:ellipsis}"
      << "th{background:#eee;font-weight:700} .num{text-align:right;white-space:nowrap}"
      << "thead{display:table-header-group} tr{page-break-inside:avoid}"
      << ".page-break{break-after:page;page-break-after:always}"
      << "</style></head><body>";
  out << "<h1>Отчет по путевым листам за " << HtmlEscape(month) << "</h1>";
  out << "<div class=\"meta\">Листов: " << rows.size() << " &nbsp; Пробег: " << FormatCsvNumber(totalDistance, 1)
      << " км &nbsp; Расход: " << FormatCsvNumber(totalFuel, 2) << " л &nbsp; Заправлено: "
      << FormatCsvNumber(totalAdded, 2) << " л</div>";

  auto writeHeader = [&]() {
    out << "<table><thead><tr>"
        << "<th style=\"width:4%\">ID</th><th style=\"width:8%\">Дата</th><th style=\"width:15%\">Авто</th>"
        << "<th style=\"width:14%\">Водитель</th><th style=\"width:8%\">Одом. нач.</th><th style=\"width:8%\">Одом. кон.</th>"
        << "<th style=\"width:7%\">Км</th><th style=\"width:8%\">Топл. нач.</th><th style=\"width:8%\">Расход</th>"
        << "<th style=\"width:8%\">Заправка</th><th style=\"width:8%\">Остаток</th><th>Примечание</th>"
        << "</tr></thead><tbody>";
  };
  writeHeader();
  int rowOnPage = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& waybill = rows[i].waybill;
    out << "<tr><td class=\"num\">" << waybill.id << "</td><td>" << HtmlEscape(waybill.date) << "</td><td>"
        << HtmlEscape(carNameById.count(waybill.car_id) ? carNameById.at(waybill.car_id) : "<unknown>") << "</td><td>"
        << HtmlEscape(driverNameById.count(waybill.driver_id) ? driverNameById.at(waybill.driver_id) : "<unknown>")
        << "</td><td class=\"num\">" << FormatCsvNumber(waybill.odometer_start, 1) << "</td><td class=\"num\">"
        << FormatCsvNumber(waybill.odometer_end, 1) << "</td><td class=\"num\">" << FormatCsvNumber(rows[i].distance, 1)
        << "</td><td class=\"num\">" << FormatCsvNumber(waybill.fuel_start_l, 2) << "</td><td class=\"num\">"
        << FormatCsvNumber(waybill.calculated_fuel_l, 2) << "</td><td class=\"num\">"
        << FormatCsvNumber(waybill.fuel_added_l, 2) << "</td><td class=\"num\">" << FormatCsvNumber(waybill.fuel_end_l, 2)
        << "</td><td>" << HtmlEscape(waybill.notes) << "</td></tr>";
    ++rowOnPage;
    if (rowOnPage >= 23 && i + 1 < rows.size()) {
      out << "</tbody></table><div class=\"page-break\"></div>";
      writeHeader();
      rowOnPage = 0;
    }
  }
  out << "</tbody></table></body></html>";
  out.close();
  if (!out) {
    uiStatus_ = "Ошибка отчета: не удалось записать файл";
    return false;
  }

  OpenFileWithDefaultApp(outPath);
  uiStatus_ = "Печатный отчет создан и открыт: " + PathToUtf8String(outPath);
  return true;
}

#if WAYSHEET_ENABLE_IMGUI
void App::applyModernStyle() const {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 10.0f;
  style.ChildRounding = 8.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 8.0f;
  style.ScrollbarRounding = 10.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 8.0f;
  style.WindowPadding = ImVec2(14, 12);
  style.FramePadding = ImVec2(10, 6);
  style.ItemSpacing = ImVec2(9, 8);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.11f, 0.15f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.20f, 0.30f, 0.44f, 0.95f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.38f, 0.56f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.35f, 0.52f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.21f, 0.33f, 0.49f, 0.95f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.41f, 0.62f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.30f, 0.46f, 1.00f);
  colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.21f, 0.29f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.41f, 0.62f, 1.00f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.24f, 0.37f, 0.56f, 1.00f);
}

int App::runGui() {
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(1420, 900, "Waysheet - Учет путевых листов", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  LoadRussianAndIconFonts(io);
  applyModernStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    drawWorkspace();

    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

void App::drawWorkspace() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
  ImGui::Begin("Рабочее пространство Waysheet", nullptr, flags);

  ImGui::Text("Waysheet %s", kIconWaybill);
  ImGui::SameLine();
  ImGui::TextDisabled("| БД: %s", db_.isOpen() ? db_.currentPath().c_str() : "<закрыта>");
  ImGui::SameLine();
  ImGui::TextDisabled("| Тема: %s", themes_.current().name.c_str());
  ImGui::Separator();

  if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
    const std::string tabDashboard = IconLabel(kIconDashboard, "Дашборд");
    const std::string tabMonthly = IconLabel(kIconSummary, "Итоги за месяц");
    const std::string tabCars = IconLabel(kIconCar, "Автомобили");
    const std::string tabDrivers = IconLabel(kIconDriver, "Водители");
    const std::string tabRoutes = IconLabel(kIconRoute, "Маршруты");
    const std::string tabWaybill = IconLabel(kIconWaybill, "Путевой лист");
    const std::string tabDb = IconLabel(kIconDb, "База данных");

    if (ImGui::BeginTabItem(tabDashboard.c_str(), nullptr, activeMainTab_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 0;
      drawDashboard();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabWaybill.c_str(), nullptr, activeMainTab_ == 5 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 5;
      drawWaybillTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabMonthly.c_str(), nullptr, activeMainTab_ == 6 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 6;
      drawMonthlySummaryTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabCars.c_str(), nullptr, activeMainTab_ == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 2;
      drawCarsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabDrivers.c_str(), nullptr, activeMainTab_ == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 3;
      drawDriversTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabRoutes.c_str(), nullptr, activeMainTab_ == 4 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 4;
      drawRoutesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tabDb.c_str(), nullptr, activeMainTab_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      activeMainTab_ = 1;
      drawDbManagerWindow();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  if (!uiStatus_.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", uiStatus_.c_str());
  }
  ImGui::End();
}

void App::drawDashboard() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float topH = 150.0f;
  const float leftW = (avail.x * 0.34f > 340.0f) ? avail.x * 0.34f : 340.0f;

  int year = 0;
  int monthNum = 0;
  int day = 0;
  FillTodayDate(year, monthNum, day);
  char currentMonth[8]{};
  std::snprintf(currentMonth, sizeof(currentMonth), "%04d-%02d", year, monthNum);

  const auto cars = db_.listCars();
  const auto drivers = db_.listDrivers();
  const auto routes = db_.listRoutes();
  const auto waybills = db_.listWaybills();
  std::unordered_map<std::int64_t, std::string> carNameById;
  std::unordered_map<std::int64_t, std::string> driverNameById;
  std::unordered_map<std::int64_t, std::string> routeNameById;
  for (const auto& car : cars) carNameById.emplace(car.id, car.plate_number + " / " + car.brand);
  for (const auto& driver : drivers) driverNameById.emplace(driver.id, driver.full_name);
  for (const auto& route : routes) routeNameById.emplace(route.id, route.name);

  int monthWaybills = 0;
  double monthDistance = 0.0;
  double monthFuel = 0.0;
  double monthAdded = 0.0;
  std::unordered_map<std::int64_t, double> distanceByCar;
  std::unordered_map<std::int64_t, double> distanceByRoute;
  std::array<float, 12> yearDistance{};
  std::unordered_map<std::int64_t, std::array<float, 12>> yearDistanceByRoute;
  for (const auto& waybill : waybills) {
    double distance = 0.0;
    for (const auto& detail : waybill.details) {
      distance += detail.distance_km;
      if (waybill.date.rfind(currentMonth, 0) == 0) distanceByRoute[detail.route_id] += detail.distance_km;
    }

    int wy = 0;
    int wm = 0;
    int wd = 0;
    if (ParseIsoDate(waybill.date.c_str(), wy, wm, wd) && wy == year && wm >= 1 && wm <= 12) {
      yearDistance[static_cast<std::size_t>(wm - 1)] += static_cast<float>(distance);
      for (const auto& detail : waybill.details) {
        yearDistanceByRoute[detail.route_id][static_cast<std::size_t>(wm - 1)] += static_cast<float>(detail.distance_km);
      }
    }

    if (waybill.date.rfind(currentMonth, 0) != 0) continue;
    ++monthWaybills;
    monthDistance += distance;
    monthFuel += waybill.calculated_fuel_l;
    monthAdded += waybill.fuel_added_l;
    distanceByCar[waybill.car_id] += distance;
  }

  ImGui::BeginChild("dash_top", ImVec2(0, topH), true);
  ImGui::Text("Текущий месяц: %s", currentMonth);
  ImGui::Separator();
  ImGui::Columns(5, "dash_stats", false);
  ImGui::Text("Листов\n%d", monthWaybills);
  ImGui::NextColumn();
  ImGui::Text("Пробег\n%.1f км", monthDistance);
  ImGui::NextColumn();
  ImGui::Text("Расход\n%.2f л", monthFuel);
  ImGui::NextColumn();
  ImGui::Text("Заправлено\n%.2f л", monthAdded);
  ImGui::NextColumn();
  ImGui::Text("Всего листов\n%lld", static_cast<long long>(db_.countRows("waybill_entries")));
  ImGui::Columns(1);
  ImGui::EndChild();

  ImGui::BeginChild("dash_left", ImVec2(leftW, 0), true);
  ImGui::Text("%s Быстрые действия", kIconDashboard);
  ImGui::Separator();
  const float actionW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (IconTextButton(kIconWaybill, "Листы", "dash_waybill", ImVec2(actionW, 28))) activeMainTab_ = 5;
  ImGui::SameLine();
  if (IconTextButton(kIconSummary, "Итоги", "dash_monthly", ImVec2(actionW, 28))) activeMainTab_ = 6;
  if (IconTextButton(kIconCar, "Авто", "dash_cars", ImVec2(actionW, 28))) activeMainTab_ = 2;
  ImGui::SameLine();
  if (IconTextButton(kIconDriver, "Водители", "dash_drivers", ImVec2(actionW, 28))) activeMainTab_ = 3;
  if (IconTextButton(kIconRoute, "Маршруты", "dash_routes", ImVec2(actionW, 28))) activeMainTab_ = 4;
  ImGui::SameLine();
  if (IconTextButton(kIconDb, "БД", "dash_db", ImVec2(actionW, 28))) activeMainTab_ = 1;
  ImGui::Separator();
  ImGui::Text("Авто: %lld", static_cast<long long>(db_.countRows("cars")));
  ImGui::Text("Водители: %lld", static_cast<long long>(db_.countRows("drivers")));
  ImGui::Text("Маршруты: %lld", static_cast<long long>(db_.countRows("routes")));
  ImGui::Separator();
  ImGui::TextUnformatted("Доли пробега по маршрутам");
  const ImVec2 pieStart = ImGui::GetCursorScreenPos();
  const float pieSize = 150.0f;
  const ImVec2 center(pieStart.x + pieSize * 0.5f, pieStart.y + pieSize * 0.5f);
  const float radius = pieSize * 0.44f;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  static const ImU32 kPieColors[] = {
      IM_COL32(82, 143, 255, 255), IM_COL32(95, 196, 136, 255), IM_COL32(244, 180, 72, 255),
      IM_COL32(232, 111, 111, 255), IM_COL32(167, 125, 235, 255), IM_COL32(74, 190, 196, 255)};
  std::vector<std::pair<std::int64_t, double>> routeRows;
  for (const auto& [routeId, distance] : distanceByRoute) routeRows.emplace_back(routeId, distance);
  std::sort(routeRows.begin(), routeRows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  if (monthDistance <= 0.0 || routeRows.empty()) {
    drawList->AddCircle(center, radius, IM_COL32(90, 96, 110, 255), 48, 2.0f);
    ImGui::Dummy(ImVec2(pieSize, pieSize));
    ImGui::TextDisabled("Нет пробега за текущий месяц");
  } else {
    float startAngle = -1.5708f;
    for (std::size_t i = 0; i < routeRows.size(); ++i) {
      const float angle = static_cast<float>((routeRows[i].second / monthDistance) * 6.28318530718);
      const int segments = std::max(8, static_cast<int>(angle * 28.0f));
      drawList->PathClear();
      drawList->PathLineTo(center);
      for (int s = 0; s <= segments; ++s) {
        const float a = startAngle + angle * (static_cast<float>(s) / static_cast<float>(segments));
        drawList->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
      }
      drawList->PathFillConvex(kPieColors[i % IM_ARRAYSIZE(kPieColors)]);
      startAngle += angle;
    }
    ImGui::Dummy(ImVec2(pieSize, pieSize));
    for (std::size_t i = 0; i < routeRows.size() && i < 5; ++i) {
      ImGui::ColorButton(("##route_color_" + std::to_string(i)).c_str(), ImGui::ColorConvertU32ToFloat4(kPieColors[i % IM_ARRAYSIZE(kPieColors)]),
                         ImGuiColorEditFlags_NoTooltip, ImVec2(10, 10));
      ImGui::SameLine();
      const auto routeIt = routeNameById.find(routeRows[i].first);
      ImGui::Text("%.0f%% %s", routeRows[i].second * 100.0 / monthDistance,
                  routeIt != routeNameById.end() ? routeIt->second.c_str() : "<unknown>");
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("dash_right", ImVec2(0, 0), true);
  ImGui::Text("Динамика пробега с начала %d года", year);
  ImGui::Separator();
  float maxYearDistance = 0.0f;
  for (const float value : yearDistance) maxYearDistance = std::max(maxYearDistance, value);
  std::vector<std::pair<std::int64_t, float>> yearRouteTotals;
  for (const auto& [routeId, values] : yearDistanceByRoute) {
    float total = 0.0f;
    for (const float value : values) total += value;
    if (total > 0.0f) yearRouteTotals.emplace_back(routeId, total);
  }
  std::sort(yearRouteTotals.begin(), yearRouteTotals.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

  const ImVec2 graphStart = ImGui::GetCursorScreenPos();
  const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, 150.0f);
  ImGui::InvisibleButton("##year_distance_graph", graphSize);
  const bool graphHovered = ImGui::IsItemHovered();
  const ImVec2 mousePos = ImGui::GetIO().MousePos;
  ImDrawList* graphDrawList = ImGui::GetWindowDrawList();
  graphDrawList->AddRectFilled(graphStart, ImVec2(graphStart.x + graphSize.x, graphStart.y + graphSize.y), IM_COL32(22, 26, 34, 255), 4.0f);
  graphDrawList->AddRect(graphStart, ImVec2(graphStart.x + graphSize.x, graphStart.y + graphSize.y), IM_COL32(70, 78, 92, 255), 4.0f);
  const float graphMax = maxYearDistance > 0.0f ? maxYearDistance * 1.1f : 1.0f;
  const float plotTop = graphStart.y + 8.0f;
  const float plotBottom = graphStart.y + graphSize.y - 28.0f;
  const float plotLeft = graphStart.x + 12.0f;
  const float plotRight = graphStart.x + graphSize.x - 12.0f;
  auto graphPoint = [&](int monthIndex, float value) {
    const float x = plotLeft + (plotRight - plotLeft) * (static_cast<float>(monthIndex) / 11.0f);
    const float y = plotBottom - (plotBottom - plotTop) * (value / graphMax);
    return ImVec2(x, y);
  };
  static const char* kMonthShort[] = {"Янв", "Фев", "Мар", "Апр", "Май", "Июн", "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек"};
  for (int i = 0; i < 12; ++i) {
    const float x = graphPoint(i, 0.0f).x;
    graphDrawList->AddLine(ImVec2(x, plotTop), ImVec2(x, plotBottom), IM_COL32(45, 52, 64, 255));
    const ImVec2 labelSize = ImGui::CalcTextSize(kMonthShort[i]);
    graphDrawList->AddText(ImVec2(x - labelSize.x * 0.5f, graphStart.y + graphSize.y - 20.0f), IM_COL32(150, 158, 172, 255),
                           kMonthShort[i]);
  }
  for (std::size_t r = 0; r < yearRouteTotals.size() && r < 5; ++r) {
    const auto& values = yearDistanceByRoute[yearRouteTotals[r].first];
    for (int i = 1; i < 12; ++i) {
      graphDrawList->AddLine(graphPoint(i - 1, values[static_cast<std::size_t>(i - 1)]), graphPoint(i, values[static_cast<std::size_t>(i)]),
                             kPieColors[r % IM_ARRAYSIZE(kPieColors)], 2.0f);
    }
  }
  for (int i = 1; i < 12; ++i) {
    graphDrawList->AddLine(graphPoint(i - 1, yearDistance[static_cast<std::size_t>(i - 1)]), graphPoint(i, yearDistance[static_cast<std::size_t>(i)]),
                           IM_COL32(240, 244, 252, 255), 3.0f);
  }
  if (graphHovered && mousePos.x >= plotLeft && mousePos.x <= plotRight) {
    int hoveredMonth = static_cast<int>(std::round(((mousePos.x - plotLeft) / (plotRight - plotLeft)) * 11.0f));
    hoveredMonth = std::clamp(hoveredMonth, 0, 11);
    const float x = graphPoint(hoveredMonth, 0.0f).x;
    graphDrawList->AddLine(ImVec2(x, plotTop), ImVec2(x, plotBottom), IM_COL32(255, 255, 255, 120), 1.5f);
    ImGui::BeginTooltip();
    ImGui::Text("%s %d", kMonthShort[hoveredMonth], year);
    ImGui::Separator();
    ImGui::Text("Итого: %.1f км", yearDistance[static_cast<std::size_t>(hoveredMonth)]);
    for (std::size_t r = 0; r < yearRouteTotals.size() && r < 5; ++r) {
      const auto routeId = yearRouteTotals[r].first;
      const auto& values = yearDistanceByRoute[routeId];
      const auto routeIt = routeNameById.find(routeId);
      ImGui::Text("%s: %.1f км", routeIt != routeNameById.end() ? routeIt->second.c_str() : "<unknown>",
                  values[static_cast<std::size_t>(hoveredMonth)]);
    }
    ImGui::EndTooltip();
  }
  ImGui::ColorButton("##year_total_color", ImGui::ColorConvertU32ToFloat4(IM_COL32(240, 244, 252, 255)), ImGuiColorEditFlags_NoTooltip,
                     ImVec2(10, 10));
  ImGui::SameLine();
  ImGui::TextUnformatted("Итого");
  for (std::size_t r = 0; r < yearRouteTotals.size() && r < 5; ++r) {
    ImGui::SameLine();
    ImGui::ColorButton(("##year_route_color_" + std::to_string(r)).c_str(), ImGui::ColorConvertU32ToFloat4(kPieColors[r % IM_ARRAYSIZE(kPieColors)]),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(10, 10));
    ImGui::SameLine();
    const auto routeIt = routeNameById.find(yearRouteTotals[r].first);
    ImGui::TextUnformatted(routeIt != routeNameById.end() ? routeIt->second.c_str() : "<unknown>");
  }
  ImGui::Spacing();

  ImGui::TextUnformatted("Последние путевые листы");
  ImGui::Separator();
  if (ImGui::BeginTable("dash_recent_waybills", 7,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 200))) {
    ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Авто");
    ImGui::TableSetupColumn("Водитель");
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();
    int shown = 0;
    for (const auto& waybill : waybills) {
      if (shown >= 12) break;
      double distance = 0.0;
      for (const auto& detail : waybill.details) distance += detail.distance_km;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(waybill.date.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(waybill.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(waybill.car_id) ? carNameById.at(waybill.car_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(driverNameById.count(waybill.driver_id) ? driverNameById.at(waybill.driver_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", waybill.calculated_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", waybill.fuel_end_l);
      ++shown;
    }
    ImGui::EndTable();
  }

  ImGui::TextUnformatted("Пробег по автомобилям за текущий месяц");
  ImGui::Separator();
  if (ImGui::BeginTable("dash_cars_month", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Автомобиль");
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Доля", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableHeadersRow();
    std::vector<std::pair<std::int64_t, double>> carRows;
    for (const auto& [carId, distance] : distanceByCar) carRows.emplace_back(carId, distance);
    std::sort(carRows.begin(), carRows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [carId, distance] : carRows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(carId) ? carNameById.at(carId).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.0f%%", monthDistance > 0.0 ? (distance * 100.0 / monthDistance) : 0.0);
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void App::drawMonthlySummaryTab() {
  static std::string selectedReportMonth;

  struct SummaryRow {
    std::int64_t id{};
    std::string period;
    std::string label;
    int waybills{};
    double distance{};
    double calculatedFuel{};
    double fuelAdded{};
    double firstOdometer{};
    double lastOdometer{};
    double firstFuel{};
    double lastFuel{};
    std::string firstKey;
    std::string lastKey;
    bool hasValues{};
  };

  auto addToSummary = [](SummaryRow& row, const WaybillEntry& waybill, double distance) {
    ++row.waybills;
    row.distance += distance;
    row.calculatedFuel += waybill.calculated_fuel_l;
    row.fuelAdded += waybill.fuel_added_l;
    const std::string key = waybill.date + "#" + std::to_string(waybill.id);
    if (!row.hasValues || key < row.firstKey) {
      row.firstOdometer = waybill.odometer_start;
      row.firstFuel = waybill.fuel_start_l;
      row.firstKey = key;
    }
    if (!row.hasValues || key > row.lastKey) {
      row.lastOdometer = waybill.odometer_end;
      row.lastFuel = waybill.fuel_end_l;
      row.lastKey = key;
    }
    row.hasValues = true;
  };

  const auto cars = db_.listCars();
  const auto drivers = db_.listDrivers();
  const auto waybills = db_.listWaybills();

  std::unordered_map<std::int64_t, std::string> carNameById;
  std::unordered_map<std::int64_t, std::string> driverNameById;
  for (const auto& car : cars) carNameById.emplace(car.id, car.plate_number + " / " + car.brand);
  for (const auto& driver : drivers) driverNameById.emplace(driver.id, driver.full_name);

  int totalWaybills = 0;
  double totalDistance = 0.0;
  double totalFuel = 0.0;
  double totalAdded = 0.0;
  std::unordered_map<std::string, SummaryRow> byMonth;
  std::unordered_map<std::string, SummaryRow> byCarMonth;
  std::unordered_map<std::string, SummaryRow> byDriverMonth;

  for (const auto& waybill : waybills) {
    if (waybill.date.size() < 7) continue;

    const std::string period = waybill.date.substr(0, 7);
    double distance = 0.0;
    for (const auto& detail : waybill.details) distance += detail.distance_km;
    ++totalWaybills;
    totalDistance += distance;
    totalFuel += waybill.calculated_fuel_l;
    totalAdded += waybill.fuel_added_l;

    auto& monthRow = byMonth[period];
    monthRow.period = period;
    monthRow.label = period;
    addToSummary(monthRow, waybill, distance);

    const std::string carKey = period + "#" + std::to_string(waybill.car_id);
    auto& carRow = byCarMonth[carKey];
    carRow.period = period;
    carRow.id = waybill.car_id;
    carRow.label = carNameById.count(waybill.car_id) ? carNameById.at(waybill.car_id) : "<unknown>";
    addToSummary(carRow, waybill, distance);

    const std::string driverKey = period + "#" + std::to_string(waybill.driver_id);
    auto& driverRow = byDriverMonth[driverKey];
    driverRow.period = period;
    driverRow.id = waybill.driver_id;
    driverRow.label = driverNameById.count(waybill.driver_id) ? driverNameById.at(waybill.driver_id) : "<unknown>";
    addToSummary(driverRow, waybill, distance);
  }

  std::vector<SummaryRow> monthRows;
  for (const auto& [_, row] : byMonth) monthRows.push_back(row);
  std::sort(monthRows.begin(), monthRows.end(), [](const SummaryRow& a, const SummaryRow& b) { return a.period > b.period; });
  if (selectedReportMonth.empty() && !monthRows.empty()) selectedReportMonth = monthRows.front().period;

  ImGui::BeginChild("monthly_summary_tab", ImVec2(0, 0), true);
  ImGui::TextUnformatted("Итоги за месяц");
  ImGui::Separator();

  ImGui::Columns(4, "monthly_kpi", false);
  ImGui::Text("Листов\n%d", totalWaybills);
  ImGui::NextColumn();
  ImGui::Text("Пробег\n%.1f км", totalDistance);
  ImGui::NextColumn();
  ImGui::Text("Расход\n%.2f л", totalFuel);
  ImGui::NextColumn();
  ImGui::Text("Заправлено\n%.2f л", totalAdded);
  ImGui::Columns(1);

  ImGui::Separator();
  ImGui::TextUnformatted("По месяцам");
  if (ImGui::BeginTable("monthly_by_month", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 180))) {
    ImGui::TableSetupColumn("Месяц", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Листов", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Заправка", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Отчет", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();
    for (const auto& row : monthRows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(row.period.c_str(), selectedReportMonth == row.period)) {
        selectedReportMonth = row.period;
      }
      ImGui::TableNextColumn();
      ImGui::Text("%d", row.waybills);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", row.distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.calculatedFuel);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.fuelAdded);
      ImGui::TableNextColumn();
      ImGui::PushID(row.period.c_str());
      if (ImGui::SmallButton("Отчет")) {
        selectedReportMonth = row.period;
        exportMonthlyWaybillReportHtml(row.period);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::TextUnformatted("По автомобилям");
  if (ImGui::BeginTable("monthly_by_car", 9,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 230))) {
    ImGui::TableSetupColumn("Месяц", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Автомобиль");
    ImGui::TableSetupColumn("Листов", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Заправка", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Одом. нач.", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Одом. кон.", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableHeadersRow();
    std::vector<SummaryRow> rows;
    for (const auto& [_, row] : byCarMonth) rows.push_back(row);
    std::sort(rows.begin(), rows.end(), [](const SummaryRow& a, const SummaryRow& b) {
      if (a.period != b.period) return a.period > b.period;
      return a.label < b.label;
    });
    for (const auto& row : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.period.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.label.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", row.waybills);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", row.distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.calculatedFuel);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.fuelAdded);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", row.firstOdometer);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", row.lastOdometer);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.lastFuel);
    }
    ImGui::EndTable();
  }

  ImGui::TextUnformatted("По водителям");
  if (ImGui::BeginTable("monthly_by_driver", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Месяц", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Водитель");
    ImGui::TableSetupColumn("Листов", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Заправка", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableHeadersRow();
    std::vector<SummaryRow> rows;
    for (const auto& [_, row] : byDriverMonth) rows.push_back(row);
    std::sort(rows.begin(), rows.end(), [](const SummaryRow& a, const SummaryRow& b) {
      if (a.period != b.period) return a.period > b.period;
      return a.label < b.label;
    });
    for (const auto& row : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.period.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.label.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", row.waybills);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", row.distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.calculatedFuel);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.fuelAdded);
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void App::drawDbManagerWindow() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float leftW = (avail.x * 0.64f > 520.0f) ? avail.x * 0.64f : 520.0f;

  ImGui::BeginChild("db_manager_left", ImVec2(leftW, 0), true);
  ImGui::TextUnformatted("Операции с базой данных");
  ImGui::Separator();

  ImGui::InputText("Путь создания", dbCreatePath_.data(), dbCreatePath_.size());
  ImGui::SameLine();
  if (IconTextButton(kIconAdd, "Создать", "db_create", ImVec2(140, 0))) createNewDatabase(dbCreatePath_.data());

  ImGui::InputText("Путь открытия", dbOpenPath_.data(), dbOpenPath_.size());
  ImGui::SameLine();
  if (IconTextButton(kIconOpen, "Открыть", "db_open", ImVec2(140, 0))) openDatabase(dbOpenPath_.data());

  ImGui::InputText("Путь Сохранить как", dbSaveAsPath_.data(), dbSaveAsPath_.size());
  ImGui::SameLine();
  if (IconTextButton(kIconSave, "Сохранить как", "db_saveas", ImVec2(160, 0))) saveDatabaseAs(dbSaveAsPath_.data());

  if (IconTextButton(kIconClose, "Закрыть текущую БД", "db_close", ImVec2(220, 0))) {
    db_.close();
    uiStatus_ = "База данных закрыта";
  }

  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("db_manager_right", ImVec2(0, 0), true);
  ImGui::TextUnformatted("Последние базы");
  ImGui::Separator();
  if (recentDbs_.empty()) ImGui::TextDisabled("Нет недавних баз");
  for (const auto& path : recentDbs_) {
    if (ImGui::Selectable(path.c_str())) openDatabase(path);
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Информация о БД");
  ImGui::TextWrapped("Путь: %s", db_.isOpen() ? db_.currentPath().c_str() : "<закрыта>");
  ImGui::Text("Автомобили: %lld", static_cast<long long>(db_.countRows("cars")));
  ImGui::Text("Водители: %lld", static_cast<long long>(db_.countRows("drivers")));
  ImGui::Text("Маршруты: %lld", static_cast<long long>(db_.countRows("routes")));
  ImGui::EndChild();
}

void App::drawCarsTab() {
  {
    static bool focusEditorField = false;
    static std::int64_t selectedCarId = 0;
    static std::array<char, 64> plateFilter{};

    const auto cars = db_.listCars();
    auto hasSelected = [&](std::int64_t id) {
      return std::any_of(cars.begin(), cars.end(), [id](const Car& c) { return c.id == id; });
    };
    if (!hasSelected(selectedCarId)) selectedCarId = 0;

    ImGui::BeginChild("cars_tab_v3", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "car_toolbar_add_v3", ImVec2(130, 0))) {
      Car car{};
      car.plate_number = carForm_.plate.data();
      car.brand = carForm_.brand.data();
      car.tank_volume_l = carForm_.tankVolume;
      car.initial_odometer = carForm_.initialOdometer;
      car.initial_fuel_l = carForm_.initialFuel;
      car.registration_date = carForm_.registrationDate.data();
      car.status = carForm_.active ? "active" : "inactive";
      if (db_.addCar(car)) {
        uiStatus_ = "Автомобиль добавлен";
        const auto refreshed = db_.listCars();
        if (!refreshed.empty()) {
          const auto& added = refreshed.front();
          selectedCarId = added.id;
          std::snprintf(carForm_.plate.data(), carForm_.plate.size(), "%s", added.plate_number.c_str());
          std::snprintf(carForm_.brand.data(), carForm_.brand.size(), "%s", added.brand.c_str());
          std::snprintf(carForm_.registrationDate.data(), carForm_.registrationDate.size(), "%s", added.registration_date.c_str());
          carForm_.tankVolume = static_cast<float>(added.tank_volume_l);
          carForm_.initialOdometer = static_cast<float>(added.initial_odometer);
          carForm_.initialFuel = static_cast<float>(added.initial_fuel_l);
          carForm_.active = (added.status == "active");
          focusEditorField = true;
        }
      } else {
        uiStatus_ = "Ошибка добавления авто: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "car_toolbar_delete_v3", ImVec2(130, 0)) && selectedCarId != 0) {
      if (db_.deleteCar(selectedCarId)) {
        uiStatus_ = "Автомобиль удален";
        selectedCarId = 0;
      } else {
        uiStatus_ = "Ошибка удаления авто: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "car_toolbar_print_v3", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Автомобили' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по номеру", plateFilter.data(), plateFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("cars_left_v3", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("cars_table_v3", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("Номер");
      ImGui::TableSetupColumn("Марка");
      ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableHeadersRow();
      const std::string filter = plateFilter.data();
      for (const auto& car : cars) {
        if (!filter.empty() && car.plate_number.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedCarId == car.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(car.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
          selectedCarId = car.id;
          std::snprintf(carForm_.plate.data(), carForm_.plate.size(), "%s", car.plate_number.c_str());
          std::snprintf(carForm_.brand.data(), carForm_.brand.size(), "%s", car.brand.c_str());
          std::snprintf(carForm_.registrationDate.data(), carForm_.registrationDate.size(), "%s", car.registration_date.c_str());
          carForm_.tankVolume = static_cast<float>(car.tank_volume_l);
          carForm_.initialOdometer = static_cast<float>(car.initial_odometer);
          carForm_.initialFuel = static_cast<float>(car.initial_fuel_l);
          carForm_.active = (car.status == "active");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.plate_number.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.brand.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.status.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("cars_right_v3", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    bool commit = false;
    if (focusEditorField) {
      ImGui::SetKeyboardFocusHere();
      focusEditorField = false;
    }
    ImGui::InputText("Гос. номер", carForm_.plate.data(), carForm_.plate.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputText("Марка", carForm_.brand.data(), carForm_.brand.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputFloat("Объем бака (л)", &carForm_.tankVolume, 1.0f, 10.0f, "%.1f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputFloat("Одометр на начало учета", &carForm_.initialOdometer, 1.0f, 10.0f, "%.1f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputFloat("Остаток топлива на начало учета", &carForm_.initialFuel, 0.1f, 1.0f, "%.2f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputText("Дата учета", carForm_.registrationDate.data(), carForm_.registrationDate.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Checkbox("Активен", &carForm_.active);
    commit |= ImGui::IsItemDeactivatedAfterEdit();

    if (commit && selectedCarId != 0) {
      Car updated{};
      updated.id = selectedCarId;
      updated.plate_number = carForm_.plate.data();
      updated.brand = carForm_.brand.data();
      updated.tank_volume_l = carForm_.tankVolume;
      updated.initial_odometer = carForm_.initialOdometer;
      updated.initial_fuel_l = carForm_.initialFuel;
      updated.registration_date = carForm_.registrationDate.data();
      updated.status = carForm_.active ? "active" : "inactive";
      if (db_.updateCar(updated)) {
        if (recalculateWaybillChainForCar(updated.id)) {
          uiStatus_ = "Изменения автомобиля сохранены, путевые листы пересчитаны";
        } else {
          uiStatus_ = "Автомобиль сохранен, но пересчет путевых листов не выполнен: " + db_.lastError();
        }
      } else {
        uiStatus_ = "Ошибка сохранения автомобиля: " + db_.lastError();
      }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Нормы расхода по трассам");
    if (selectedCarId == 0) {
      ImGui::TextDisabled("Выберите автомобиль слева");
    } else {
      const auto routesForNorms = db_.listRoutes();
      const auto carNorms = db_.listCarRouteNorms(selectedCarId);
      std::unordered_map<std::int64_t, CarRouteNorm> normByRouteId;
      for (const auto& norm : carNorms) normByRouteId.emplace(norm.route_id, norm);

      if (routesForNorms.empty()) {
        ImGui::TextDisabled("Сначала добавьте трассы/типы маршрутов");
      } else if (ImGui::BeginTable("car_route_norms_v3", 3,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Трасса");
        ImGui::TableSetupColumn("Лето л/100", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Зима л/100", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();
        for (const auto& route : routesForNorms) {
          auto it = normByRouteId.find(route.id);
          double summer = it != normByRouteId.end() ? it->second.norm_summer_l100 : route.norm_summer_l100;
          double winter = it != normByRouteId.end() ? it->second.norm_winter_l100 : route.norm_winter_l100;

          ImGui::PushID(static_cast<int>(route.id));
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(route.name.c_str());
          ImGui::TableNextColumn();
          bool normChanged = ImGui::InputDouble("##summer", &summer, 0.1, 1.0, "%.2f");
          ImGui::TableNextColumn();
          normChanged |= ImGui::InputDouble("##winter", &winter, 0.1, 1.0, "%.2f");
          if (normChanged) {
            CarRouteNorm norm{};
            norm.car_id = selectedCarId;
            norm.route_id = route.id;
            norm.norm_summer_l100 = summer;
            norm.norm_winter_l100 = winter;
            if (db_.upsertCarRouteNorm(norm)) {
              if (recalculateWaybillChainForCar(selectedCarId)) {
                uiStatus_ = "Норма расхода автомобиля сохранена, путевые листы пересчитаны";
              } else {
                uiStatus_ = "Норма сохранена, но пересчет путевых листов не выполнен: " + db_.lastError();
              }
            } else {
              uiStatus_ = "Ошибка сохранения нормы расхода: " + db_.lastError();
            }
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  {
    static std::int64_t selectedCarId = 0;
    static std::array<char, 64> plateFilter{};

    const auto cars = db_.listCars();
    auto findById = [&](std::int64_t id) -> const Car* {
      for (const auto& c : cars) {
        if (c.id == id) return &c;
      }
      return nullptr;
    };

    ImGui::BeginChild("cars_tab_v2", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "car_toolbar_add_v2", ImVec2(130, 0))) {
      Car car{};
      car.plate_number = carForm_.plate.data();
      car.brand = carForm_.brand.data();
      car.tank_volume_l = carForm_.tankVolume;
      car.initial_odometer = carForm_.initialOdometer;
      car.initial_fuel_l = carForm_.initialFuel;
      car.registration_date = carForm_.registrationDate.data();
      car.status = carForm_.active ? "active" : "inactive";
      if (db_.addCar(car)) {
        uiStatus_ = "Автомобиль добавлен";
      } else {
        uiStatus_ = "Ошибка добавления авто: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "car_toolbar_delete_v2", ImVec2(130, 0)) && selectedCarId != 0) {
      if (db_.deleteCar(selectedCarId)) {
        uiStatus_ = "Автомобиль удален";
        selectedCarId = 0;
      } else {
        uiStatus_ = "Ошибка удаления авто: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconOpen, "Редактировать", "car_toolbar_edit_v2", ImVec2(160, 0))) {
      if (const Car* c = findById(selectedCarId)) {
        std::snprintf(carForm_.plate.data(), carForm_.plate.size(), "%s", c->plate_number.c_str());
        std::snprintf(carForm_.brand.data(), carForm_.brand.size(), "%s", c->brand.c_str());
        std::snprintf(carForm_.registrationDate.data(), carForm_.registrationDate.size(), "%s", c->registration_date.c_str());
        carForm_.tankVolume = static_cast<float>(c->tank_volume_l);
        carForm_.initialOdometer = static_cast<float>(c->initial_odometer);
        carForm_.initialFuel = static_cast<float>(c->initial_fuel_l);
        carForm_.active = (c->status == "active");
        uiStatus_ = "Карточка загружена в редактор";
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "car_toolbar_print_v2", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Автомобили' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по номеру", plateFilter.data(), plateFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("cars_left_v2", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("cars_table_v2", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("Номер");
      ImGui::TableSetupColumn("Марка");
      ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableHeadersRow();
      const std::string filter = plateFilter.data();
      for (const auto& car : cars) {
        if (!filter.empty() && car.plate_number.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedCarId == car.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(car.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) selectedCarId = car.id;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.plate_number.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.brand.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(car.status.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("cars_right_v2", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    ImGui::InputText("Гос. номер", carForm_.plate.data(), carForm_.plate.size());
    ImGui::InputText("Марка", carForm_.brand.data(), carForm_.brand.size());
    ImGui::InputFloat("Объем бака (л)", &carForm_.tankVolume, 1.0f, 10.0f, "%.1f");
    ImGui::InputFloat("Одометр на начало учета", &carForm_.initialOdometer, 1.0f, 10.0f, "%.1f");
    ImGui::InputFloat("Остаток топлива на начало учета", &carForm_.initialFuel, 0.1f, 1.0f, "%.2f");
    ImGui::InputText("Дата учета", carForm_.registrationDate.data(), carForm_.registrationDate.size());
    ImGui::Checkbox("Активен", &carForm_.active);
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  ImGui::BeginChild("cars_tab", ImVec2(0, 0), true);
  ImGui::Columns(2, "cars_cols", false);

  ImGui::InputText("Гос. номер", carForm_.plate.data(), carForm_.plate.size());
  ImGui::InputText("Марка", carForm_.brand.data(), carForm_.brand.size());
  ImGui::InputFloat("Объем бака (л)", &carForm_.tankVolume, 1.0f, 10.0f, "%.1f");
  ImGui::InputFloat("Одометр на начало учета", &carForm_.initialOdometer, 1.0f, 10.0f, "%.1f");
  ImGui::InputFloat("Остаток топлива на начало учета", &carForm_.initialFuel, 0.1f, 1.0f, "%.2f");
  ImGui::InputText("Дата учета", carForm_.registrationDate.data(), carForm_.registrationDate.size());
  ImGui::Checkbox("Активен", &carForm_.active);
  if (IconTextButton(kIconAdd, "Добавить авто", "car_add", ImVec2(180, 0))) {
    Car car{};
    car.plate_number = carForm_.plate.data();
    car.brand = carForm_.brand.data();
    car.tank_volume_l = carForm_.tankVolume;
    car.initial_odometer = carForm_.initialOdometer;
    car.initial_fuel_l = carForm_.initialFuel;
    car.registration_date = carForm_.registrationDate.data();
    car.status = carForm_.active ? "active" : "inactive";
    if (db_.addCar(car)) {
      uiStatus_ = "Автомобиль добавлен";
      carForm_.plate[0] = '\0';
      carForm_.brand[0] = '\0';
    } else {
      uiStatus_ = "Ошибка добавления авто: " + db_.lastError();
    }
  }

  ImGui::NextColumn();
  const auto cars = db_.listCars();
  if (ImGui::BeginTable("cars_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Номер");
    ImGui::TableSetupColumn("Марка");
    ImGui::TableSetupColumn("Бак");
    ImGui::TableSetupColumn("Статус");
    ImGui::TableSetupColumn("Действие");
    ImGui::TableHeadersRow();
    for (const auto& car : cars) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(car.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(car.plate_number.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(car.brand.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", car.tank_volume_l);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(car.status.c_str());
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(car.id));
      if (IconTextButton(kIconDelete, "Удалить", "car_del", ImVec2(110, 0))) {
        if (db_.deleteCar(car.id)) uiStatus_ = "Автомобиль удален";
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::Columns(1);
#if 0

  ImGui::Separator();
  ImGui::TextUnformatted("Журнал путевых листов");
  ImGui::InputText("С даты", waybillForm_.filterDateFrom.data(), waybillForm_.filterDateFrom.size());
  ImGui::SameLine();
  ImGui::InputText("По дату", waybillForm_.filterDateTo.data(), waybillForm_.filterDateTo.size());

  const std::string carFilterPreview =
      (waybillForm_.filterCarId != 0 && carNameById.count(waybillForm_.filterCarId) > 0) ? carNameById.at(waybillForm_.filterCarId)
                                                                                           : "Все авто";
  if (ImGui::BeginCombo("Фильтр авто", carFilterPreview.c_str())) {
    if (ImGui::Selectable("Все авто", waybillForm_.filterCarId == 0)) waybillForm_.filterCarId = 0;
    for (const auto& c : cars) {
      const bool selected = c.id == waybillForm_.filterCarId;
      const std::string label = c.plate_number + " / " + c.brand;
      if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.filterCarId = c.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  const std::string driverFilterPreview = (waybillForm_.filterDriverId != 0 && driverNameById.count(waybillForm_.filterDriverId) > 0)
                                              ? driverNameById.at(waybillForm_.filterDriverId)
                                              : "Все водители";
  if (ImGui::BeginCombo("Фильтр водитель", driverFilterPreview.c_str())) {
    if (ImGui::Selectable("Все водители", waybillForm_.filterDriverId == 0)) waybillForm_.filterDriverId = 0;
    for (const auto& d : drivers) {
      const bool selected = d.id == waybillForm_.filterDriverId;
      if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.filterDriverId = d.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto rows = db_.listWaybills();
  bool deletedAny = false;
  if (ImGui::BeginTable("waybill_journal", 10,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 220))) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Авто");
    ImGui::TableSetupColumn("Водитель");
    ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Норм, л", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Факт, л", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Откл., л", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Открыть", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Удалить", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableHeadersRow();

    for (const auto& w : rows) {
      if (waybillForm_.filterCarId != 0 && w.car_id != waybillForm_.filterCarId) continue;
      if (waybillForm_.filterDriverId != 0 && w.driver_id != waybillForm_.filterDriverId) continue;

      const std::string dateFrom = waybillForm_.filterDateFrom.data();
      const std::string dateTo = waybillForm_.filterDateTo.data();
      if (!dateFrom.empty() && w.date < dateFrom) continue;
      if (!dateTo.empty() && w.date > dateTo) continue;

      double distance = 0.0;
      for (const auto& d : w.details) distance += d.distance_km;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(w.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.date.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(w.car_id) ? carNameById.at(w.car_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(driverNameById.count(w.driver_id) ? driverNameById.at(w.driver_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.calculated_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.actual_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.variance_l);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(w.id));
      if (IconTextButton(kIconOpen, "Открыть", "wb_open", ImVec2(-1, 0))) waybillForm_.selectedWaybillId = w.id;
      ImGui::TableNextColumn();
      if (IconTextButton(kIconDelete, "Удалить", "wb_delete", ImVec2(-1, 0))) {
        if (db_.deleteWaybill(w.id)) {
          uiStatus_ = "Путевой лист удален";
          if (waybillForm_.selectedWaybillId == w.id) waybillForm_.selectedWaybillId = 0;
          deletedAny = true;
        } else {
          uiStatus_ = "Ошибка удаления: " + db_.lastError();
        }
      }
      ImGui::PopID();
      if (deletedAny) break;
    }
    ImGui::EndTable();
  }

  if (waybillForm_.selectedWaybillId != 0) {
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const WaybillEntry& x) { return x.id == waybillForm_.selectedWaybillId; });
    if (selected == rows.end()) {
      waybillForm_.selectedWaybillId = 0;
    } else {
      ImGui::Separator();
      ImGui::Text("Карточка путевого листа №%lld", static_cast<long long>(selected->id));
      ImGui::BulletText("Дата: %s", selected->date.c_str());
      ImGui::BulletText("Авто: %s", carNameById.count(selected->car_id) ? carNameById.at(selected->car_id).c_str() : "<unknown>");
      ImGui::BulletText("Водитель: %s",
                        driverNameById.count(selected->driver_id) ? driverNameById.at(selected->driver_id).c_str() : "<unknown>");
      ImGui::BulletText("Одометр: %.1f -> %.1f", selected->odometer_start, selected->odometer_end);
      ImGui::BulletText("Норматив: %.2f л; Факт: %.2f л; Отклонение: %.2f л", selected->calculated_fuel_l, selected->actual_fuel_l,
                        selected->variance_l);
      if (!selected->notes.empty()) ImGui::TextWrapped("Примечание: %s", selected->notes.c_str());

      if (ImGui::BeginTable("waybill_details_selected", 5,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                            ImVec2(0, 140))) {
        ImGui::TableSetupColumn("Маршрут");
        ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Сезон", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Норма", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Норматив, л", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableHeadersRow();
        for (const auto& d : selected->details) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(routeById.count(d.route_id) ? routeById.at(d.route_id).name.c_str() : "<unknown>");
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.distance_km);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.season.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.norm_l100);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.calculated_fuel_l);
        }
        ImGui::EndTable();
      }
    }
  }

#endif
  ImGui::EndChild();
}

void App::drawDriversTab() {
  {
    static std::int64_t selectedDriverId = 0;
    static std::array<char, 96> nameFilter{};
    static bool focusEditorField = false;

    const auto drivers = db_.listDrivers();
    auto hasSelected = [&](std::int64_t id) {
      return std::any_of(drivers.begin(), drivers.end(), [id](const Driver& d) { return d.id == id; });
    };
    if (!hasSelected(selectedDriverId)) selectedDriverId = 0;

    ImGui::BeginChild("drivers_tab_v3", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "driver_toolbar_add_v3", ImVec2(130, 0))) {
      Driver d{};
      d.full_name = driverForm_.fullName.data();
      d.phone = driverForm_.phone.data();
      d.license_no = driverForm_.license.data();
      d.status = driverForm_.active ? "active" : "inactive";
      if (db_.addDriver(d)) {
        uiStatus_ = "Водитель добавлен";
        const auto refreshed = db_.listDrivers();
        if (!refreshed.empty()) {
          const auto& added = refreshed.front();
          selectedDriverId = added.id;
          std::snprintf(driverForm_.fullName.data(), driverForm_.fullName.size(), "%s", added.full_name.c_str());
          std::snprintf(driverForm_.phone.data(), driverForm_.phone.size(), "%s", added.phone.c_str());
          std::snprintf(driverForm_.license.data(), driverForm_.license.size(), "%s", added.license_no.c_str());
          driverForm_.active = (added.status == "active");
          focusEditorField = true;
        }
      } else {
        uiStatus_ = "Ошибка добавления водителя: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "driver_toolbar_delete_v3", ImVec2(130, 0)) && selectedDriverId != 0) {
      if (db_.deleteDriver(selectedDriverId)) {
        uiStatus_ = "Водитель удален";
        selectedDriverId = 0;
      } else {
        uiStatus_ = "Ошибка удаления водителя: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "driver_toolbar_print_v3", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Водители' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по ФИО", nameFilter.data(), nameFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("drivers_left_v3", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("drivers_table_v3", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("ФИО");
      ImGui::TableSetupColumn("Телефон");
      ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableHeadersRow();
      const std::string filter = nameFilter.data();
      for (const auto& row : drivers) {
        if (!filter.empty() && row.full_name.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedDriverId == row.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(row.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
          selectedDriverId = row.id;
          std::snprintf(driverForm_.fullName.data(), driverForm_.fullName.size(), "%s", row.full_name.c_str());
          std::snprintf(driverForm_.phone.data(), driverForm_.phone.size(), "%s", row.phone.c_str());
          std::snprintf(driverForm_.license.data(), driverForm_.license.size(), "%s", row.license_no.c_str());
          driverForm_.active = (row.status == "active");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.full_name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.phone.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.status.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("drivers_right_v3", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    bool commit = false;
    if (focusEditorField) {
      ImGui::SetKeyboardFocusHere();
      focusEditorField = false;
    }
    ImGui::InputText("ФИО", driverForm_.fullName.data(), driverForm_.fullName.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputText("Телефон", driverForm_.phone.data(), driverForm_.phone.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputText("Права", driverForm_.license.data(), driverForm_.license.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Checkbox("Активен", &driverForm_.active);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (commit && selectedDriverId != 0) {
      Driver updated{};
      updated.id = selectedDriverId;
      updated.full_name = driverForm_.fullName.data();
      updated.phone = driverForm_.phone.data();
      updated.license_no = driverForm_.license.data();
      updated.status = driverForm_.active ? "active" : "inactive";
      if (db_.updateDriver(updated)) {
        uiStatus_ = "Изменения водителя сохранены";
      } else {
        uiStatus_ = "Ошибка сохранения водителя: " + db_.lastError();
      }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  {
    static std::int64_t selectedDriverId = 0;
    static std::array<char, 96> nameFilter{};

    const auto drivers = db_.listDrivers();
    auto findById = [&](std::int64_t id) -> const Driver* {
      for (const auto& d : drivers) {
        if (d.id == id) return &d;
      }
      return nullptr;
    };

    ImGui::BeginChild("drivers_tab_v2", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "driver_toolbar_add_v2", ImVec2(130, 0))) {
      Driver d{};
      d.full_name = driverForm_.fullName.data();
      d.phone = driverForm_.phone.data();
      d.license_no = driverForm_.license.data();
      d.status = driverForm_.active ? "active" : "inactive";
      if (db_.addDriver(d)) {
        uiStatus_ = "Водитель добавлен";
      } else {
        uiStatus_ = "Ошибка добавления водителя: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "driver_toolbar_delete_v2", ImVec2(130, 0)) && selectedDriverId != 0) {
      if (db_.deleteDriver(selectedDriverId)) {
        uiStatus_ = "Водитель удален";
        selectedDriverId = 0;
      } else {
        uiStatus_ = "Ошибка удаления водителя: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconOpen, "Редактировать", "driver_toolbar_edit_v2", ImVec2(160, 0))) {
      if (const Driver* d = findById(selectedDriverId)) {
        std::snprintf(driverForm_.fullName.data(), driverForm_.fullName.size(), "%s", d->full_name.c_str());
        std::snprintf(driverForm_.phone.data(), driverForm_.phone.size(), "%s", d->phone.c_str());
        std::snprintf(driverForm_.license.data(), driverForm_.license.size(), "%s", d->license_no.c_str());
        driverForm_.active = (d->status == "active");
        uiStatus_ = "Карточка водителя загружена в редактор";
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "driver_toolbar_print_v2", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Водители' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по ФИО", nameFilter.data(), nameFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("drivers_left_v2", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("drivers_table_v2", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("ФИО");
      ImGui::TableSetupColumn("Телефон");
      ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableHeadersRow();
      const std::string filter = nameFilter.data();
      for (const auto& row : drivers) {
        if (!filter.empty() && row.full_name.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedDriverId == row.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(row.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) selectedDriverId = row.id;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.full_name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.phone.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.status.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("drivers_right_v2", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    ImGui::InputText("ФИО", driverForm_.fullName.data(), driverForm_.fullName.size());
    ImGui::InputText("Телефон", driverForm_.phone.data(), driverForm_.phone.size());
    ImGui::InputText("Права", driverForm_.license.data(), driverForm_.license.size());
    ImGui::Checkbox("Активен", &driverForm_.active);
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  ImGui::BeginChild("drivers_tab", ImVec2(0, 0), true);
  ImGui::Columns(2, "drivers_cols", false);

  ImGui::InputText("ФИО", driverForm_.fullName.data(), driverForm_.fullName.size());
  ImGui::InputText("Телефон", driverForm_.phone.data(), driverForm_.phone.size());
  ImGui::InputText("Права", driverForm_.license.data(), driverForm_.license.size());
  ImGui::Checkbox("Активен", &driverForm_.active);
  if (IconTextButton(kIconAdd, "Добавить водителя", "driver_add", ImVec2(220, 0))) {
    Driver d{};
    d.full_name = driverForm_.fullName.data();
    d.phone = driverForm_.phone.data();
    d.license_no = driverForm_.license.data();
    d.status = driverForm_.active ? "active" : "inactive";
    if (db_.addDriver(d)) {
      uiStatus_ = "Водитель добавлен";
      driverForm_.fullName[0] = '\0';
      driverForm_.phone[0] = '\0';
      driverForm_.license[0] = '\0';
    } else {
      uiStatus_ = "Ошибка добавления водителя: " + db_.lastError();
    }
  }

  ImGui::NextColumn();
  const auto rows = db_.listDrivers();
  if (ImGui::BeginTable("drivers_table", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("ФИО");
    ImGui::TableSetupColumn("Телефон");
    ImGui::TableSetupColumn("Права");
    ImGui::TableSetupColumn("Статус");
    ImGui::TableSetupColumn("Действие");
    ImGui::TableHeadersRow();
    for (const auto& row : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(row.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.full_name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.phone.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.license_no.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.status.c_str());
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row.id));
      if (IconTextButton(kIconDelete, "Удалить", "driver_del", ImVec2(110, 0))) {
        if (db_.deleteDriver(row.id)) uiStatus_ = "Водитель удален";
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::Columns(1);
#if 0

  ImGui::Separator();
  ImGui::TextUnformatted("Р–СѓСЂРЅР°Р» РїСѓС‚РµРІС‹С… Р»РёСЃС‚РѕРІ");
  ImGui::InputText("РЎ РґР°С‚С‹", waybillForm_.filterDateFrom.data(), waybillForm_.filterDateFrom.size());
  ImGui::SameLine();
  ImGui::InputText("РџРѕ РґР°С‚Сѓ", waybillForm_.filterDateTo.data(), waybillForm_.filterDateTo.size());

  const std::string carFilterPreview =
      (waybillForm_.filterCarId != 0 && carNameById.count(waybillForm_.filterCarId) > 0) ? carNameById.at(waybillForm_.filterCarId)
                                                                                           : "Р’СЃРµ Р°РІС‚Рѕ";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ Р°РІС‚Рѕ", carFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ Р°РІС‚Рѕ", waybillForm_.filterCarId == 0)) waybillForm_.filterCarId = 0;
    for (const auto& c : cars) {
      const bool selected = c.id == waybillForm_.filterCarId;
      const std::string label = c.plate_number + " / " + c.brand;
      if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.filterCarId = c.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  const std::string driverFilterPreview = (waybillForm_.filterDriverId != 0 && driverNameById.count(waybillForm_.filterDriverId) > 0)
                                              ? driverNameById.at(waybillForm_.filterDriverId)
                                              : "Р’СЃРµ РІРѕРґРёС‚РµР»Рё";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ РІРѕРґРёС‚РµР»СЊ", driverFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ РІРѕРґРёС‚РµР»Рё", waybillForm_.filterDriverId == 0)) waybillForm_.filterDriverId = 0;
    for (const auto& d : drivers) {
      const bool selected = d.id == waybillForm_.filterDriverId;
      if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.filterDriverId = d.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto rows = db_.listWaybills();
  bool deletedAny = false;
  if (ImGui::BeginTable("waybill_journal", 10,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 220))) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Р”Р°С‚Р°", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("РђРІС‚Рѕ");
    ImGui::TableSetupColumn("Р’РѕРґРёС‚РµР»СЊ");
    ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("РќРѕСЂРј, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Р¤Р°РєС‚, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєР»., Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєСЂС‹С‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("РЈРґР°Р»РёС‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableHeadersRow();

    for (const auto& w : rows) {
      if (waybillForm_.filterCarId != 0 && w.car_id != waybillForm_.filterCarId) continue;
      if (waybillForm_.filterDriverId != 0 && w.driver_id != waybillForm_.filterDriverId) continue;

      const std::string dateFrom = waybillForm_.filterDateFrom.data();
      const std::string dateTo = waybillForm_.filterDateTo.data();
      if (!dateFrom.empty() && w.date < dateFrom) continue;
      if (!dateTo.empty() && w.date > dateTo) continue;

      double distance = 0.0;
      for (const auto& d : w.details) distance += d.distance_km;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(w.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.date.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(w.car_id) ? carNameById.at(w.car_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(driverNameById.count(w.driver_id) ? driverNameById.at(w.driver_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.calculated_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.actual_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.variance_l);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(w.id));
      if (IconTextButton(kIconOpen, "РћС‚РєСЂС‹С‚СЊ", "wb_open", ImVec2(-1, 0))) waybillForm_.selectedWaybillId = w.id;
      ImGui::TableNextColumn();
      if (IconTextButton(kIconDelete, "РЈРґР°Р»РёС‚СЊ", "wb_delete", ImVec2(-1, 0))) {
        if (db_.deleteWaybill(w.id)) {
          uiStatus_ = "РџСѓС‚РµРІРѕР№ Р»РёСЃС‚ СѓРґР°Р»РµРЅ";
          if (waybillForm_.selectedWaybillId == w.id) waybillForm_.selectedWaybillId = 0;
          deletedAny = true;
        } else {
          uiStatus_ = "РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ: " + db_.lastError();
        }
      }
      ImGui::PopID();
      if (deletedAny) break;
    }
    ImGui::EndTable();
  }

  if (waybillForm_.selectedWaybillId != 0) {
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const WaybillEntry& x) { return x.id == waybillForm_.selectedWaybillId; });
    if (selected == rows.end()) {
      waybillForm_.selectedWaybillId = 0;
    } else {
      ImGui::Separator();
      ImGui::Text("РљР°СЂС‚РѕС‡РєР° РїСѓС‚РµРІРѕРіРѕ Р»РёСЃС‚Р° в„–%lld", static_cast<long long>(selected->id));
      ImGui::BulletText("Р”Р°С‚Р°: %s", selected->date.c_str());
      ImGui::BulletText("РђРІС‚Рѕ: %s", carNameById.count(selected->car_id) ? carNameById.at(selected->car_id).c_str() : "<unknown>");
      ImGui::BulletText("Р’РѕРґРёС‚РµР»СЊ: %s",
                        driverNameById.count(selected->driver_id) ? driverNameById.at(selected->driver_id).c_str() : "<unknown>");
      ImGui::BulletText("РћРґРѕРјРµС‚СЂ: %.1f -> %.1f", selected->odometer_start, selected->odometer_end);
      ImGui::BulletText("РќРѕСЂРјР°С‚РёРІ: %.2f Р»; Р¤Р°РєС‚: %.2f Р»; РћС‚РєР»РѕРЅРµРЅРёРµ: %.2f Р»", selected->calculated_fuel_l, selected->actual_fuel_l,
                        selected->variance_l);
      if (!selected->notes.empty()) ImGui::TextWrapped("РџСЂРёРјРµС‡Р°РЅРёРµ: %s", selected->notes.c_str());

      if (ImGui::BeginTable("waybill_details_selected", 4,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                            ImVec2(0, 140))) {
        ImGui::TableSetupColumn("РњР°СЂС€СЂСѓС‚");
        ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("РЎРµР·РѕРЅ", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("РќРѕСЂРјР°С‚РёРІ, Р»", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableHeadersRow();
        for (const auto& d : selected->details) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(routeById.count(d.route_id) ? routeById.at(d.route_id).name.c_str() : "<unknown>");
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.distance_km);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.season.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.calculated_fuel_l);
        }
        ImGui::EndTable();
      }
    }
  }

  ImGui::EndChild();
#endif
  ImGui::EndChild();
}

void App::drawRoutesTab() {
  {
    static std::int64_t selectedRouteId = 0;
    static std::array<char, 96> nameFilter{};
    static bool focusEditorField = false;

    const auto routes = db_.listRoutes();
    auto hasSelected = [&](std::int64_t id) {
      return std::any_of(routes.begin(), routes.end(), [id](const Route& r) { return r.id == id; });
    };
    if (!hasSelected(selectedRouteId)) selectedRouteId = 0;

    ImGui::BeginChild("routes_tab_v3", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "route_toolbar_add_v3", ImVec2(130, 0))) {
      Route r{};
      r.name = routeForm_.name.data();
      r.norm_summer_l100 = 0.0;
      r.norm_winter_l100 = 0.0;
      if (db_.addRoute(r)) {
        uiStatus_ = "Маршрут добавлен";
        const auto refreshed = db_.listRoutes();
        if (!refreshed.empty()) {
          const auto& added = refreshed.front();
          selectedRouteId = added.id;
          std::snprintf(routeForm_.name.data(), routeForm_.name.size(), "%s", added.name.c_str());
          focusEditorField = true;
        }
      } else {
        uiStatus_ = "Ошибка добавления маршрута: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "route_toolbar_delete_v3", ImVec2(130, 0)) && selectedRouteId != 0) {
      if (db_.deleteRoute(selectedRouteId)) {
        uiStatus_ = "Маршрут удален";
        selectedRouteId = 0;
      } else {
        uiStatus_ = "Ошибка удаления маршрута: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "route_toolbar_print_v3", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Маршруты' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по названию", nameFilter.data(), nameFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("routes_left_v3", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("routes_table_v3", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("Название");
      ImGui::TableHeadersRow();
      const std::string filter = nameFilter.data();
      for (const auto& row : routes) {
        if (!filter.empty() && row.name.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedRouteId == row.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(row.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
          selectedRouteId = row.id;
          std::snprintf(routeForm_.name.data(), routeForm_.name.size(), "%s", row.name.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.name.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("routes_right_v3", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    bool commit = false;
    if (focusEditorField) {
      ImGui::SetKeyboardFocusHere();
      focusEditorField = false;
    }
    ImGui::InputText("Название маршрута", routeForm_.name.data(), routeForm_.name.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (commit && selectedRouteId != 0) {
      Route updated{};
      updated.id = selectedRouteId;
      updated.name = routeForm_.name.data();
      if (const auto selected = std::find_if(routes.begin(), routes.end(), [&](const Route& route) { return route.id == selectedRouteId; });
          selected != routes.end()) {
        updated.norm_summer_l100 = selected->norm_summer_l100;
        updated.norm_winter_l100 = selected->norm_winter_l100;
      }
      if (db_.updateRoute(updated)) {
        uiStatus_ = "Изменения маршрута сохранены";
      } else {
        uiStatus_ = "Ошибка сохранения маршрута: " + db_.lastError();
      }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  {
    static std::int64_t selectedRouteId = 0;
    static std::array<char, 96> nameFilter{};

    const auto routes = db_.listRoutes();
    auto findById = [&](std::int64_t id) -> const Route* {
      for (const auto& r : routes) {
        if (r.id == id) return &r;
      }
      return nullptr;
    };

    ImGui::BeginChild("routes_tab_v2", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "route_toolbar_add_v2", ImVec2(130, 0))) {
      Route r{};
      r.name = routeForm_.name.data();
      r.norm_summer_l100 = routeForm_.normSummer;
      r.norm_winter_l100 = routeForm_.normWinter;
      if (db_.addRoute(r)) {
        uiStatus_ = "Маршрут добавлен";
      } else {
        uiStatus_ = "Ошибка добавления маршрута: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "route_toolbar_delete_v2", ImVec2(130, 0)) && selectedRouteId != 0) {
      if (db_.deleteRoute(selectedRouteId)) {
        uiStatus_ = "Маршрут удален";
        selectedRouteId = 0;
      } else {
        uiStatus_ = "Ошибка удаления маршрута: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconOpen, "Редактировать", "route_toolbar_edit_v2", ImVec2(160, 0))) {
      if (const Route* r = findById(selectedRouteId)) {
        std::snprintf(routeForm_.name.data(), routeForm_.name.size(), "%s", r->name.c_str());
        routeForm_.normSummer = static_cast<float>(r->norm_summer_l100);
        routeForm_.normWinter = static_cast<float>(r->norm_winter_l100);
        uiStatus_ = "Карточка маршрута загружена в редактор";
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "route_toolbar_print_v2", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Маршруты' будет добавлена";
    }

    ImGui::Separator();
    ImGui::InputText("Фильтр по названию", nameFilter.data(), nameFilter.size());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftW = avail.x * 0.50f;
    if (leftW < 360.0f) leftW = 360.0f;
    if (leftW > avail.x - 280.0f) leftW = avail.x - 280.0f;

    ImGui::BeginChild("routes_left_v2", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("routes_table_v2", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("Название");
      ImGui::TableSetupColumn("Лето", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Зима", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableHeadersRow();
      const std::string filter = nameFilter.data();
      for (const auto& row : routes) {
        if (!filter.empty() && row.name.find(filter) == std::string::npos) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedRouteId == row.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(row.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) selectedRouteId = row.id;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", row.norm_summer_l100);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", row.norm_winter_l100);
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("routes_right_v2", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();
    ImGui::InputText("Название маршрута", routeForm_.name.data(), routeForm_.name.size());
    ImGui::InputFloat("Норма лето (л/100км)", &routeForm_.normSummer, 0.1f, 1.0f, "%.2f");
    ImGui::InputFloat("Норма зима (л/100км)", &routeForm_.normWinter, 0.1f, 1.0f, "%.2f");
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
  ImGui::BeginChild("routes_tab", ImVec2(0, 0), true);
  ImGui::Columns(2, "routes_cols", false);

  ImGui::InputText("Название маршрута", routeForm_.name.data(), routeForm_.name.size());
  ImGui::InputFloat("Норма лето (л/100км)", &routeForm_.normSummer, 0.1f, 1.0f, "%.2f");
  ImGui::InputFloat("Норма зима (л/100км)", &routeForm_.normWinter, 0.1f, 1.0f, "%.2f");
  if (IconTextButton(kIconAdd, "Добавить маршрут", "route_add", ImVec2(220, 0))) {
    Route r{};
    r.name = routeForm_.name.data();
    r.norm_summer_l100 = routeForm_.normSummer;
    r.norm_winter_l100 = routeForm_.normWinter;
    if (db_.addRoute(r)) {
      uiStatus_ = "Маршрут добавлен";
      routeForm_.name[0] = '\0';
    } else {
      uiStatus_ = "Ошибка добавления маршрута: " + db_.lastError();
    }
  }

  ImGui::NextColumn();
  const auto rows = db_.listRoutes();
  if (ImGui::BeginTable("routes_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Название");
    ImGui::TableSetupColumn("Лето");
    ImGui::TableSetupColumn("Зима");
    ImGui::TableSetupColumn("Действие");
    ImGui::TableHeadersRow();
    for (const auto& row : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(row.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.norm_summer_l100);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.norm_winter_l100);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row.id));
      if (IconTextButton(kIconDelete, "Удалить", "route_del", ImVec2(110, 0))) {
        if (db_.deleteRoute(row.id)) uiStatus_ = "Маршрут удален";
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::Columns(1);
#if 0

  ImGui::Separator();
  ImGui::TextUnformatted("Р–СѓСЂРЅР°Р» РїСѓС‚РµРІС‹С… Р»РёСЃС‚РѕРІ");
  ImGui::InputText("РЎ РґР°С‚С‹", waybillForm_.filterDateFrom.data(), waybillForm_.filterDateFrom.size());
  ImGui::SameLine();
  ImGui::InputText("РџРѕ РґР°С‚Сѓ", waybillForm_.filterDateTo.data(), waybillForm_.filterDateTo.size());

  const std::string carFilterPreview =
      (waybillForm_.filterCarId != 0 && carNameById.count(waybillForm_.filterCarId) > 0) ? carNameById.at(waybillForm_.filterCarId)
                                                                                           : "Р’СЃРµ Р°РІС‚Рѕ";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ Р°РІС‚Рѕ", carFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ Р°РІС‚Рѕ", waybillForm_.filterCarId == 0)) waybillForm_.filterCarId = 0;
    for (const auto& c : cars) {
      const bool selected = c.id == waybillForm_.filterCarId;
      const std::string label = c.plate_number + " / " + c.brand;
      if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.filterCarId = c.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  const std::string driverFilterPreview = (waybillForm_.filterDriverId != 0 && driverNameById.count(waybillForm_.filterDriverId) > 0)
                                              ? driverNameById.at(waybillForm_.filterDriverId)
                                              : "Р’СЃРµ РІРѕРґРёС‚РµР»Рё";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ РІРѕРґРёС‚РµР»СЊ", driverFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ РІРѕРґРёС‚РµР»Рё", waybillForm_.filterDriverId == 0)) waybillForm_.filterDriverId = 0;
    for (const auto& d : drivers) {
      const bool selected = d.id == waybillForm_.filterDriverId;
      if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.filterDriverId = d.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto rows = db_.listWaybills();
  bool deletedAny = false;
  if (ImGui::BeginTable("waybill_journal", 10,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 220))) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Р”Р°С‚Р°", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("РђРІС‚Рѕ");
    ImGui::TableSetupColumn("Р’РѕРґРёС‚РµР»СЊ");
    ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("РќРѕСЂРј, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Р¤Р°РєС‚, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєР»., Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєСЂС‹С‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("РЈРґР°Р»РёС‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableHeadersRow();

    for (const auto& w : rows) {
      if (waybillForm_.filterCarId != 0 && w.car_id != waybillForm_.filterCarId) continue;
      if (waybillForm_.filterDriverId != 0 && w.driver_id != waybillForm_.filterDriverId) continue;

      const std::string dateFrom = waybillForm_.filterDateFrom.data();
      const std::string dateTo = waybillForm_.filterDateTo.data();
      if (!dateFrom.empty() && w.date < dateFrom) continue;
      if (!dateTo.empty() && w.date > dateTo) continue;

      double distance = 0.0;
      for (const auto& d : w.details) distance += d.distance_km;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(w.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.date.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(w.car_id) ? carNameById.at(w.car_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(driverNameById.count(w.driver_id) ? driverNameById.at(w.driver_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.calculated_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.actual_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.variance_l);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(w.id));
      if (IconTextButton(kIconOpen, "РћС‚РєСЂС‹С‚СЊ", "wb_open", ImVec2(-1, 0))) waybillForm_.selectedWaybillId = w.id;
      ImGui::TableNextColumn();
      if (IconTextButton(kIconDelete, "РЈРґР°Р»РёС‚СЊ", "wb_delete", ImVec2(-1, 0))) {
        if (db_.deleteWaybill(w.id)) {
          uiStatus_ = "РџСѓС‚РµРІРѕР№ Р»РёСЃС‚ СѓРґР°Р»РµРЅ";
          if (waybillForm_.selectedWaybillId == w.id) waybillForm_.selectedWaybillId = 0;
          deletedAny = true;
        } else {
          uiStatus_ = "РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ: " + db_.lastError();
        }
      }
      ImGui::PopID();
      if (deletedAny) break;
    }
    ImGui::EndTable();
  }

  if (waybillForm_.selectedWaybillId != 0) {
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const WaybillEntry& x) { return x.id == waybillForm_.selectedWaybillId; });
    if (selected == rows.end()) {
      waybillForm_.selectedWaybillId = 0;
    } else {
      ImGui::Separator();
      ImGui::Text("РљР°СЂС‚РѕС‡РєР° РїСѓС‚РµРІРѕРіРѕ Р»РёСЃС‚Р° в„–%lld", static_cast<long long>(selected->id));
      ImGui::BulletText("Р”Р°С‚Р°: %s", selected->date.c_str());
      ImGui::BulletText("РђРІС‚Рѕ: %s", carNameById.count(selected->car_id) ? carNameById.at(selected->car_id).c_str() : "<unknown>");
      ImGui::BulletText("Р’РѕРґРёС‚РµР»СЊ: %s",
                        driverNameById.count(selected->driver_id) ? driverNameById.at(selected->driver_id).c_str() : "<unknown>");
      ImGui::BulletText("РћРґРѕРјРµС‚СЂ: %.1f -> %.1f", selected->odometer_start, selected->odometer_end);
      ImGui::BulletText("РќРѕСЂРјР°С‚РёРІ: %.2f Р»; Р¤Р°РєС‚: %.2f Р»; РћС‚РєР»РѕРЅРµРЅРёРµ: %.2f Р»", selected->calculated_fuel_l, selected->actual_fuel_l,
                        selected->variance_l);
      if (!selected->notes.empty()) ImGui::TextWrapped("РџСЂРёРјРµС‡Р°РЅРёРµ: %s", selected->notes.c_str());

      if (ImGui::BeginTable("waybill_details_selected", 4,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                            ImVec2(0, 140))) {
        ImGui::TableSetupColumn("РњР°СЂС€СЂСѓС‚");
        ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("РЎРµР·РѕРЅ", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("РќРѕСЂРјР°С‚РёРІ, Р»", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableHeadersRow();
        for (const auto& d : selected->details) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(routeById.count(d.route_id) ? routeById.at(d.route_id).name.c_str() : "<unknown>");
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.distance_km);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.season.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.calculated_fuel_l);
        }
        ImGui::EndTable();
      }
    }
  }

  ImGui::EndChild();
#endif
  ImGui::EndChild();
}

void App::drawWaybillTab() {
  {
    static bool focusEditorField = false;
    static int calendarYear = 0;
    static int calendarMonth = 0;
    static bool waybillEditorExpanded = false;
    const auto cars = db_.listCars();
    const auto drivers = db_.listDrivers();
    const auto routes = db_.listRoutes();
    if (cars.empty() || drivers.empty() || routes.empty()) {
      ImGui::BeginChild("waybill_tab_v3_empty", ImVec2(0, 0), true);
      ImGui::TextWrapped("Для путевого листа сначала заполните справочники: авто, водители, маршруты.");
      ImGui::EndChild();
      return;
    }

    std::unordered_map<std::int64_t, Route> routeById;
    std::unordered_map<std::int64_t, std::string> carNameById;
    std::unordered_map<std::int64_t, std::string> driverNameById;
    for (const auto& r : routes) routeById.emplace(r.id, r);
    for (const auto& c : cars) carNameById.emplace(c.id, c.plate_number + " / " + c.brand);
    for (const auto& d : drivers) driverNameById.emplace(d.id, d.full_name);

    static int selectedDetailIndex = -1;
    static bool focusDetailEditorField = false;
    auto hasCar = [&](std::int64_t id) {
      return std::any_of(cars.begin(), cars.end(), [id](const Car& c) { return c.id == id; });
    };
    auto hasDriver = [&](std::int64_t id) {
      return std::any_of(drivers.begin(), drivers.end(), [id](const Driver& d) { return d.id == id; });
    };
    auto hasRoute = [&](std::int64_t id) {
      return std::any_of(routes.begin(), routes.end(), [id](const Route& r) { return r.id == id; });
    };
    if (!hasCar(waybillForm_.selectedCarId)) waybillForm_.selectedCarId = cars.front().id;
    if (!hasDriver(waybillForm_.selectedDriverId)) waybillForm_.selectedDriverId = drivers.front().id;
    if (!hasRoute(waybillForm_.selectedRouteId)) waybillForm_.selectedRouteId = routes.front().id;

    auto buildWaybillFromForm = [&](std::int64_t id) {
      WaybillEntry entry{};
      entry.id = id;
      entry.car_id = waybillForm_.selectedCarId;
      entry.driver_id = waybillForm_.selectedDriverId;
      entry.date = waybillForm_.date.data();
      entry.odometer_start = waybillForm_.odometerStart;
      entry.fuel_start_l = waybillForm_.fuelStart;
      entry.fuel_added_l = waybillForm_.fuelAdded;
      entry.notes = waybillForm_.notes.data();

      if (id == 0) {
        const auto opening = FindOpeningBalance(cars, db_.listWaybills(), entry.car_id);
        entry.odometer_start = opening.odometer;
        entry.fuel_start_l = opening.fuel_l;
      }

      double totalDistance = 0.0;
      double totalNorm = 0.0;
      std::unordered_map<std::int64_t, CarRouteNorm> carNormByRouteId;
      for (const auto& norm : db_.listCarRouteNorms(entry.car_id)) {
        carNormByRouteId.emplace(norm.route_id, norm);
      }
      for (const auto& d : waybillForm_.details) {
        auto it = routeById.find(d.route_id);
        if (it == routeById.end()) continue;
        WaybillRouteDetail out = d;
        const auto carNormIt = carNormByRouteId.find(d.route_id);
        const double norm =
            carNormIt != carNormByRouteId.end()
                ? (d.season == "summer" ? carNormIt->second.norm_summer_l100 : carNormIt->second.norm_winter_l100)
                : (d.season == "summer" ? it->second.norm_summer_l100 : it->second.norm_winter_l100);
        out.calculated_fuel_l = fuel::calcNormativeFuel(d.distance_km, norm);
        entry.details.push_back(out);
        totalDistance += d.distance_km;
        totalNorm += out.calculated_fuel_l;
      }
      entry.odometer_end = entry.odometer_start + totalDistance;
      entry.calculated_fuel_l = totalNorm;
      entry.fuel_end_l = entry.fuel_start_l + entry.fuel_added_l - entry.calculated_fuel_l;
      entry.actual_fuel_l = entry.calculated_fuel_l;
      entry.variance_l = fuel::calcVariance(entry.actual_fuel_l, entry.calculated_fuel_l);
      return entry;
    };

    ImGui::BeginChild("waybill_tab_v3", ImVec2(0, 0), true);
    if (IconTextButton(kIconAdd, "Добавить", "wb_toolbar_add_v3", ImVec2(130, 0))) {
      int todayY = 0;
      int todayM = 0;
      int todayD = 0;
      FillTodayDate(todayY, todayM, todayD);
      FormatIsoDate(todayY, todayM, todayD, waybillForm_.date.data(), waybillForm_.date.size());
      waybillForm_.fuelAdded = 0.0F;
      waybillForm_.fuelEnd = 0.0F;
      waybillForm_.notes[0] = '\0';
      waybillForm_.details.clear();
      selectedDetailIndex = -1;
      const auto opening = FindOpeningBalance(cars, db_.listWaybills(), waybillForm_.selectedCarId);
      waybillForm_.odometerStart = static_cast<float>(opening.odometer);
      waybillForm_.fuelStart = static_cast<float>(opening.fuel_l);
      if (db_.addWaybill(buildWaybillFromForm(0))) {
        if (recalculateWaybillChainForCar(waybillForm_.selectedCarId)) {
          uiStatus_ = "Путевой лист добавлен, цепочка остатков пересчитана";
        } else {
          uiStatus_ = "Путевой лист добавлен, но пересчет цепочки не выполнен: " + db_.lastError();
        }
        const auto refreshed = db_.listWaybills();
        if (!refreshed.empty()) {
          const auto& added = *std::max_element(refreshed.begin(), refreshed.end(), [](const WaybillEntry& a, const WaybillEntry& b) {
            return a.id < b.id;
          });
          waybillForm_.selectedWaybillId = added.id;
          std::snprintf(waybillForm_.date.data(), waybillForm_.date.size(), "%s", added.date.c_str());
          waybillForm_.odometerStart = static_cast<float>(added.odometer_start);
          waybillForm_.fuelStart = static_cast<float>(added.fuel_start_l);
          waybillForm_.fuelAdded = static_cast<float>(added.fuel_added_l);
          waybillForm_.fuelEnd = static_cast<float>(added.fuel_end_l);
          std::snprintf(waybillForm_.notes.data(), waybillForm_.notes.size(), "%s", added.notes.c_str());
          waybillForm_.selectedCarId = added.car_id;
          waybillForm_.selectedDriverId = added.driver_id;
          waybillForm_.details = added.details;
          if (!waybillForm_.details.empty()) waybillForm_.selectedRouteId = waybillForm_.details.front().route_id;
          selectedDetailIndex = waybillForm_.details.empty() ? -1 : 0;
          focusEditorField = true;
        }
      } else {
        uiStatus_ = "Ошибка добавления путевого листа: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить", "wb_toolbar_delete_v3", ImVec2(130, 0)) && waybillForm_.selectedWaybillId != 0) {
      std::int64_t deletedCarId = 0;
      for (const auto& waybill : db_.listWaybills()) {
        if (waybill.id == waybillForm_.selectedWaybillId) {
          deletedCarId = waybill.car_id;
          break;
        }
      }
      if (db_.deleteWaybill(waybillForm_.selectedWaybillId)) {
        if (recalculateWaybillChainForCar(deletedCarId)) {
          uiStatus_ = "Путевой лист удален, цепочка остатков пересчитана";
        } else {
          uiStatus_ = "Путевой лист удален, но пересчет цепочки не выполнен: " + db_.lastError();
        }
        waybillForm_.selectedWaybillId = 0;
      } else {
        uiStatus_ = "Ошибка удаления путевого листа: " + db_.lastError();
      }
    }
    ImGui::SameLine();
    if (IconTextButton(kIconWaybill, "Печать", "wb_toolbar_print_v3", ImVec2(110, 0))) {
      uiStatus_ = "Печать для раздела 'Путевые листы' будет добавлена";
    }

      ImGui::SameLine();
    if (IconTextButton(kIconSave, U8(u8"Экспорт CSV"), "wb_toolbar_export_csv_v3", ImVec2(160, 0))) {
      const auto exportRows = db_.listWaybills();
      const auto outPath = DefaultWaybillCsvPath(db_.currentPath());
      std::string error;
      if (ExportWaybillJournalCsv(outPath, exportRows, carNameById, driverNameById, waybillForm_.filterCarId,
                                  waybillForm_.filterDriverId, waybillForm_.filterDateFrom.data(),
                                  waybillForm_.filterDateTo.data(), error)) {
        uiStatus_ = std::string(U8(u8"CSV экспортирован: ")) + PathToUtf8String(outPath);
      } else {
        uiStatus_ = std::string(U8(u8"Ошибка экспорта CSV: ")) + error;
      }
    }

    ImGui::Separator();
    ImGui::InputText("С даты", waybillForm_.filterDateFrom.data(), waybillForm_.filterDateFrom.size());
    ImGui::SameLine();
    ImGui::InputText("По дату", waybillForm_.filterDateTo.data(), waybillForm_.filterDateTo.size());

    const std::string carFilterPreview =
        (waybillForm_.filterCarId != 0 && carNameById.count(waybillForm_.filterCarId)) ? carNameById.at(waybillForm_.filterCarId) : "Все авто";
    if (ImGui::BeginCombo("Фильтр авто", carFilterPreview.c_str())) {
      if (ImGui::Selectable("Все авто", waybillForm_.filterCarId == 0)) waybillForm_.filterCarId = 0;
      for (const auto& c : cars) {
        const bool selected = (c.id == waybillForm_.filterCarId);
        const std::string label = c.plate_number + " / " + c.brand;
        if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.filterCarId = c.id;
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    const std::string driverFilterPreview = (waybillForm_.filterDriverId != 0 && driverNameById.count(waybillForm_.filterDriverId))
                                                ? driverNameById.at(waybillForm_.filterDriverId)
                                                : "Все водители";
    if (ImGui::BeginCombo("Фильтр водитель", driverFilterPreview.c_str())) {
      if (ImGui::Selectable("Все водители", waybillForm_.filterDriverId == 0)) waybillForm_.filterDriverId = 0;
      for (const auto& d : drivers) {
        const bool selected = (d.id == waybillForm_.filterDriverId);
        if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.filterDriverId = d.id;
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float minListW = waybillEditorExpanded ? 420.0f : 700.0f;
    const float minEditorW = waybillEditorExpanded ? 560.0f : 320.0f;
    float leftW = avail.x * (waybillEditorExpanded ? 0.40f : 0.68f);
    if (avail.x > minListW + minEditorW) {
      if (leftW < minListW) leftW = minListW;
      if (leftW > avail.x - minEditorW) leftW = avail.x - minEditorW;
    } else {
      leftW = avail.x * (waybillEditorExpanded ? 0.42f : 0.62f);
    }

    auto rows = db_.listWaybills();

    ImGui::BeginChild("wb_left_v3", ImVec2(leftW, 0), true);
    if (ImGui::BeginTable("waybill_list_v3", 11,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Авто", ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableSetupColumn("Водитель", ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableSetupColumn("Одом. нач.", ImGuiTableColumnFlags_WidthFixed, 95.0f);
      ImGui::TableSetupColumn("Топл. нач.", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Расход", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Заправка", ImGuiTableColumnFlags_WidthFixed, 85.0f);
      ImGui::TableSetupColumn("Остаток", ImGuiTableColumnFlags_WidthFixed, 85.0f);
      ImGui::TableSetupColumn("Одом. кон.", ImGuiTableColumnFlags_WidthFixed, 95.0f);
      ImGui::TableHeadersRow();
      for (const auto& w : rows) {
        if (waybillForm_.filterCarId != 0 && w.car_id != waybillForm_.filterCarId) continue;
        if (waybillForm_.filterDriverId != 0 && w.driver_id != waybillForm_.filterDriverId) continue;
        const std::string dateFrom = waybillForm_.filterDateFrom.data();
        const std::string dateTo = waybillForm_.filterDateTo.data();
        if (!dateFrom.empty() && w.date < dateFrom) continue;
        if (!dateTo.empty() && w.date > dateTo) continue;
        double distance = 0.0;
        for (const auto& d : w.details) distance += d.distance_km;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (waybillForm_.selectedWaybillId == w.id);
        char idLabel[32];
        std::snprintf(idLabel, sizeof(idLabel), "%lld", static_cast<long long>(w.id));
        if (ImGui::Selectable(idLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) {
          waybillForm_.selectedWaybillId = w.id;
          std::snprintf(waybillForm_.date.data(), waybillForm_.date.size(), "%s", w.date.c_str());
          waybillForm_.odometerStart = static_cast<float>(w.odometer_start);
          waybillForm_.fuelStart = static_cast<float>(w.fuel_start_l);
          waybillForm_.fuelAdded = static_cast<float>(w.fuel_added_l);
          waybillForm_.fuelEnd = static_cast<float>(w.fuel_end_l);
          std::snprintf(waybillForm_.notes.data(), waybillForm_.notes.size(), "%s", w.notes.c_str());
          waybillForm_.selectedCarId = w.car_id;
          waybillForm_.selectedDriverId = w.driver_id;
          waybillForm_.details = w.details;
          if (!w.details.empty()) waybillForm_.selectedRouteId = w.details.front().route_id;
          selectedDetailIndex = waybillForm_.details.empty() ? -1 : 0;
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(w.date.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(carNameById.count(w.car_id) ? carNameById.at(w.car_id).c_str() : "<unknown>");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(driverNameById.count(w.driver_id) ? driverNameById.at(w.driver_id).c_str() : "<unknown>");
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", w.odometer_start);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", w.fuel_start_l);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", distance);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", w.calculated_fuel_l);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", w.fuel_added_l);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", w.fuel_end_l);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", w.odometer_end);
      }
      ImGui::EndTable();
    }
    const bool listPaneActive = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::EndChild();
    if (listPaneActive) waybillEditorExpanded = false;

    ImGui::SameLine();
    ImGui::BeginChild("wb_right_v3", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Редактор");
    ImGui::Separator();

    bool commit = false;
    if (focusEditorField) {
      ImGui::SetKeyboardFocusHere();
      focusEditorField = false;
    }
    ImGui::InputText("Дата", waybillForm_.date.data(), waybillForm_.date.size());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (IconTextButton(kIconOpen, "Календарь", "wb_date_picker_open_v3", ImVec2(140, 0))) {
      int y = 0;
      int m = 0;
      int d = 0;
      if (!ParseIsoDate(waybillForm_.date.data(), y, m, d)) FillTodayDate(y, m, d);
      calendarYear = y;
      calendarMonth = m;
      ImGui::OpenPopup("wb_date_picker_popup_v3");
    }
    if (ImGui::BeginPopup("wb_date_picker_popup_v3")) {
      static const char* kMonthNamesRu[] = {"Январь",   "Февраль", "Март",    "Апрель",
                                            "Май",      "Июнь",    "Июль",    "Август",
                                            "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"};
      if (calendarMonth < 1 || calendarMonth > 12 || calendarYear < 1900) {
        int y = 0;
        int m = 0;
        int d = 0;
        if (!ParseIsoDate(waybillForm_.date.data(), y, m, d)) FillTodayDate(y, m, d);
        calendarYear = y;
        calendarMonth = m;
      }
      if (ImGui::ArrowButton("##wb_date_prev_m", ImGuiDir_Left)) {
        --calendarMonth;
        if (calendarMonth < 1) {
          calendarMonth = 12;
          --calendarYear;
        }
      }
    ImGui::SameLine();
      ImGui::Text("%s %d", kMonthNamesRu[calendarMonth - 1], calendarYear);
      ImGui::SameLine();
      if (ImGui::ArrowButton("##wb_date_next_m", ImGuiDir_Right)) {
        ++calendarMonth;
        if (calendarMonth > 12) {
          calendarMonth = 1;
          ++calendarYear;
        }
      }

      int selectedY = 0;
      int selectedM = 0;
      int selectedD = 0;
      const bool hasSelectedDate = ParseIsoDate(waybillForm_.date.data(), selectedY, selectedM, selectedD);
      if (ImGui::BeginTable("wb_date_picker_grid_v3", 7, ImGuiTableFlags_SizingFixedFit)) {
        const char* kWeekDays[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        for (int i = 0; i < 7; ++i) {
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(kWeekDays[i]);
        }
        const int firstDayCol = WeekdayMondayFirst(calendarYear, calendarMonth, 1);
        const int dayCount = DaysInMonth(calendarYear, calendarMonth);
        int day = 1;
        for (int cell = 0; cell < 42; ++cell) {
          ImGui::TableNextColumn();
          if (cell < firstDayCol || day > dayCount) {
            ImGui::TextUnformatted(" ");
            continue;
          }
          const bool isSelected =
              hasSelectedDate && selectedY == calendarYear && selectedM == calendarMonth && selectedD == day;
          if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
          }
          char dayLabel[8];
          std::snprintf(dayLabel, sizeof(dayLabel), "%d", day);
          if (ImGui::Button(dayLabel, ImVec2(28.0f, 0.0f))) {
            FormatIsoDate(calendarYear, calendarMonth, day, waybillForm_.date.data(), waybillForm_.date.size());
            commit = true;
            ImGui::CloseCurrentPopup();
          }
          if (isSelected) ImGui::PopStyleColor(3);
          ++day;
        }
        ImGui::EndTable();
      }
      ImGui::EndPopup();
    }
    const WaybillEntry previewEntry = buildWaybillFromForm(waybillForm_.selectedWaybillId);
    ImGui::Text("Одометр на начало: %.1f", previewEntry.odometer_start);
    ImGui::Text("Топливо на начало: %.2f л", previewEntry.fuel_start_l);
    ImGui::Text("Одометр на конец: %.1f", previewEntry.odometer_end);
    ImGui::Text("Расход по норме: %.2f л", previewEntry.calculated_fuel_l);
    ImGui::Text("Остаток топлива на конец: %.2f л", previewEntry.fuel_end_l);
    ImGui::InputFloat("Заправлено", &waybillForm_.fuelAdded, 0.1f, 1.0f, "%.2f");
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputTextMultiline("Примечание", waybillForm_.notes.data(), waybillForm_.notes.size(), ImVec2(-1, 70));
    commit |= ImGui::IsItemDeactivatedAfterEdit();

    auto carPreview = carNameById.count(waybillForm_.selectedCarId) ? carNameById.at(waybillForm_.selectedCarId)
                                                                     : carNameById.at(cars.front().id);
    if (ImGui::BeginCombo("Авто", carPreview.c_str())) {
      for (const auto& c : cars) {
        const bool selected = (c.id == waybillForm_.selectedCarId);
        const std::string label = c.plate_number + " / " + c.brand;
        if (ImGui::Selectable(label.c_str(), selected)) {
          waybillForm_.selectedCarId = c.id;
          const auto opening = FindOpeningBalance(cars, rows, waybillForm_.selectedCarId, waybillForm_.selectedWaybillId);
          waybillForm_.odometerStart = static_cast<float>(opening.odometer);
          waybillForm_.fuelStart = static_cast<float>(opening.fuel_l);
          commit = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    auto driverPreview = driverNameById.count(waybillForm_.selectedDriverId) ? driverNameById.at(waybillForm_.selectedDriverId)
                                                                               : driverNameById.at(drivers.front().id);
    if (ImGui::BeginCombo("Водитель", driverPreview.c_str())) {
      for (const auto& d : drivers) {
        const bool selected = (d.id == waybillForm_.selectedDriverId);
        if (ImGui::Selectable(d.full_name.c_str(), selected)) {
          waybillForm_.selectedDriverId = d.id;
          commit = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Участки / трассы");
    if (selectedDetailIndex >= static_cast<int>(waybillForm_.details.size())) selectedDetailIndex = -1;

    if (IconTextButton(kIconAdd, "Добавить участок", "wb_add_seg_v3", ImVec2(180, 0))) {
      const int month = ParseMonthFromDate(waybillForm_.date.data());
      WaybillRouteDetail d{};
      d.route_id = waybillForm_.selectedRouteId;
      d.distance_km = 0.0;
      d.season = fuel::detectSeason(month);
      waybillForm_.details.push_back(d);
      selectedDetailIndex = static_cast<int>(waybillForm_.details.size()) - 1;
      focusDetailEditorField = true;
      commit = true;
    }
    ImGui::SameLine();
    if (IconTextButton(kIconDelete, "Удалить участок", "wb_del_seg_selected_v3", ImVec2(180, 0))) {
      if (selectedDetailIndex >= 0 && selectedDetailIndex < static_cast<int>(waybillForm_.details.size())) {
        waybillForm_.details.erase(waybillForm_.details.begin() + selectedDetailIndex);
        if (selectedDetailIndex >= static_cast<int>(waybillForm_.details.size())) {
          selectedDetailIndex = static_cast<int>(waybillForm_.details.size()) - 1;
        }
        commit = true;
      }
    }

    const ImVec2 detailsAvail = ImGui::GetContentRegionAvail();
    float detailsLeftW = detailsAvail.x * 0.56f;
    if (detailsLeftW < 280.0f) detailsLeftW = 280.0f;
    if (detailsLeftW > detailsAvail.x - 220.0f) detailsLeftW = detailsAvail.x - 220.0f;

    ImGui::BeginChild("wb_details_left_v3", ImVec2(detailsLeftW, 170), true);
    if (ImGui::BeginTable("wb_details_v3", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
      ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
      ImGui::TableSetupColumn("Маршрут");
      ImGui::TableSetupColumn("Км", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Сезон", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableHeadersRow();
      for (int i = 0; i < static_cast<int>(waybillForm_.details.size()); ++i) {
        const auto& d = waybillForm_.details[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = (selectedDetailIndex == i);
        char idxLabel[16];
        std::snprintf(idxLabel, sizeof(idxLabel), "%d", i + 1);
        if (ImGui::Selectable(idxLabel, selected, ImGuiSelectableFlags_SpanAllColumns)) selectedDetailIndex = i;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(routeById.count(d.route_id) ? routeById.at(d.route_id).name.c_str() : "<unknown>");
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", d.distance_km);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(d.season.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("wb_details_right_v3", ImVec2(0, 170), true);
    ImGui::TextUnformatted("Редактор участка");
    ImGui::Separator();
    if (selectedDetailIndex >= 0 && selectedDetailIndex < static_cast<int>(waybillForm_.details.size())) {
      auto& seg = waybillForm_.details[selectedDetailIndex];
      auto segRoutePreview = routeById.count(seg.route_id) ? routeById.at(seg.route_id).name : routeById.at(routes.front().id).name;
      if (ImGui::BeginCombo("Маршрут участка", segRoutePreview.c_str())) {
        for (const auto& r : routes) {
          const bool selected = (r.id == seg.route_id);
          if (ImGui::Selectable(r.name.c_str(), selected)) {
            seg.route_id = r.id;
            commit = true;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (focusDetailEditorField) {
        ImGui::SetKeyboardFocusHere();
        focusDetailEditorField = false;
      }
      ImGui::InputDouble("Км участка", &seg.distance_km, 0.1, 1.0, "%.2f");
      commit |= ImGui::IsItemDeactivatedAfterEdit();
      const char* seasonItems[] = {"summer", "winter"};
      int seasonIdx = (seg.season == "winter") ? 1 : 0;
      if (ImGui::Combo("Сезон участка", &seasonIdx, seasonItems, IM_ARRAYSIZE(seasonItems))) {
        seg.season = seasonItems[seasonIdx];
        commit = true;
      }
    } else {
      ImGui::TextDisabled("Выберите участок слева");
    }
    ImGui::EndChild();

    if (commit && waybillForm_.selectedWaybillId != 0) {
      std::int64_t previousCarId = 0;
      for (const auto& waybill : rows) {
        if (waybill.id == waybillForm_.selectedWaybillId) {
          previousCarId = waybill.car_id;
          break;
        }
      }
      WaybillEntry updated = buildWaybillFromForm(waybillForm_.selectedWaybillId);
      if (db_.updateWaybill(updated)) {
        bool recalcOk = true;
        if (previousCarId != 0 && previousCarId != updated.car_id) recalcOk = recalculateWaybillChainForCar(previousCarId);
        recalcOk = recalculateWaybillChainForCar(updated.car_id) && recalcOk;
        if (recalcOk) {
          uiStatus_ = "Изменения путевого листа сохранены, цепочка остатков пересчитана";
          for (const auto& refreshed : db_.listWaybills()) {
            if (refreshed.id != updated.id) continue;
            waybillForm_.odometerStart = static_cast<float>(refreshed.odometer_start);
            waybillForm_.fuelStart = static_cast<float>(refreshed.fuel_start_l);
            waybillForm_.fuelEnd = static_cast<float>(refreshed.fuel_end_l);
            break;
          }
        } else {
          uiStatus_ = "Путевой лист сохранен, но пересчет цепочки не выполнен: " + db_.lastError();
        }
      } else {
        uiStatus_ = "Ошибка сохранения путевого листа: " + db_.lastError();
      }
    }

    const bool editorPaneActive = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::EndChild();
    if (editorPaneActive) waybillEditorExpanded = true;
    ImGui::EndChild();
    return;
  }
  ImGui::BeginChild("waybill_tab", ImVec2(0, 0), true);
  ImGui::Text("%s Путевой лист", kIconWaybill);
  ImGui::Separator();

  const auto cars = db_.listCars();
  const auto drivers = db_.listDrivers();
  const auto routes = db_.listRoutes();
  if (cars.empty() || drivers.empty() || routes.empty()) {
    ImGui::TextWrapped("Для создания путевого листа нужно заполнить справочники: авто, водители, маршруты.");
    ImGui::EndChild();
    return;
  }

  if (waybillForm_.selectedCarId == 0) waybillForm_.selectedCarId = cars.front().id;
  if (waybillForm_.selectedDriverId == 0) waybillForm_.selectedDriverId = drivers.front().id;
  if (waybillForm_.selectedRouteId == 0) waybillForm_.selectedRouteId = routes.front().id;

  auto carLabel = [&]() {
    for (const auto& c : cars)
      if (c.id == waybillForm_.selectedCarId) return c.plate_number + " / " + c.brand;
    return cars.front().plate_number + " / " + cars.front().brand;
  };
  auto driverLabel = [&]() {
    for (const auto& d : drivers)
      if (d.id == waybillForm_.selectedDriverId) return d.full_name;
    return drivers.front().full_name;
  };
  auto routeLabel = [&]() {
    for (const auto& r : routes)
      if (r.id == waybillForm_.selectedRouteId) return r.name;
    return routes.front().name;
  };

  std::unordered_map<std::int64_t, Route> routeById;
  for (const auto& r : routes) routeById.emplace(r.id, r);
  std::unordered_map<std::int64_t, std::string> carNameById;
  std::unordered_map<std::int64_t, std::string> driverNameById;
  for (const auto& c : cars) carNameById.emplace(c.id, c.plate_number + " / " + c.brand);
  for (const auto& d : drivers) driverNameById.emplace(d.id, d.full_name);

  ImGui::Columns(2, "wb_cols", false);
  ImGui::InputText("Дата", waybillForm_.date.data(), waybillForm_.date.size());
  ImGui::InputFloat("Одометр старт", &waybillForm_.odometerStart, 1.0f, 10.0f, "%.1f");
  ImGui::InputFloat("Топливо старт", &waybillForm_.fuelStart, 0.1f, 1.0f, "%.2f");
  ImGui::InputFloat("Заправлено", &waybillForm_.fuelAdded, 0.1f, 1.0f, "%.2f");
  ImGui::InputFloat("Топливо финиш", &waybillForm_.fuelEnd, 0.1f, 1.0f, "%.2f");
  ImGui::InputTextMultiline("Примечание", waybillForm_.notes.data(), waybillForm_.notes.size(), ImVec2(-1, 90));

  const std::string carPreview = carLabel();
  if (ImGui::BeginCombo("Автомобиль", carPreview.c_str())) {
    for (const auto& c : cars) {
      const bool selected = c.id == waybillForm_.selectedCarId;
      const std::string label = c.plate_number + " / " + c.brand;
      if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.selectedCarId = c.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const std::string driverPreview = driverLabel();
  if (ImGui::BeginCombo("Водитель", driverPreview.c_str())) {
    for (const auto& d : drivers) {
      const bool selected = d.id == waybillForm_.selectedDriverId;
      if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.selectedDriverId = d.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Добавление участка");
  const std::string routePreview = routeLabel();
  if (ImGui::BeginCombo("Маршрут", routePreview.c_str())) {
    for (const auto& r : routes) {
      const bool selected = r.id == waybillForm_.selectedRouteId;
      if (ImGui::Selectable(r.name.c_str(), selected)) waybillForm_.selectedRouteId = r.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::InputFloat("Дистанция, км", &waybillForm_.newRouteDistance, 0.1f, 1.0f, "%.2f");
  const int month = ParseMonthFromDate(waybillForm_.date.data());
  const std::string season = fuel::detectSeason(month);
  ImGui::Text("Сезон: %s", season.c_str());
  if (IconTextButton(kIconAdd, "Добавить участок", "wb_add_seg", ImVec2(220, 0))) {
    if (waybillForm_.newRouteDistance > 0.0f) {
      WaybillRouteDetail d{};
      d.route_id = waybillForm_.selectedRouteId;
      d.distance_km = waybillForm_.newRouteDistance;
      d.season = season;
      waybillForm_.details.push_back(d);
      waybillForm_.newRouteDistance = 0.0f;
    }
  }

  ImGui::NextColumn();

  double totalDistance = 0.0;
  double totalNorm = 0.0;
  for (const auto& d : waybillForm_.details) {
    totalDistance += d.distance_km;
    auto it = routeById.find(d.route_id);
    if (it == routeById.end()) continue;
    const double norm = d.season == "summer" ? it->second.norm_summer_l100 : it->second.norm_winter_l100;
    totalNorm += fuel::calcNormativeFuel(d.distance_km, norm);
  }
  const double actualFuel = fuel::calcActualFuel(waybillForm_.fuelStart, waybillForm_.fuelAdded, waybillForm_.fuelEnd);
  const double odometerEnd = waybillForm_.odometerStart + totalDistance;
  const double variance = fuel::calcVariance(actualFuel, totalNorm);

  ImGui::TextUnformatted("Итоги");
  ImGui::Separator();
  ImGui::BulletText("Пробег: %.2f км", totalDistance);
  ImGui::BulletText("Одометр финиш: %.2f", odometerEnd);
  ImGui::BulletText("Норматив: %.2f л", totalNorm);
  ImGui::BulletText("Факт: %.2f л", actualFuel);
  ImGui::BulletText("Отклонение: %.2f л", variance);

  if (ImGui::BeginTable("wb_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("№");
    ImGui::TableSetupColumn("Маршрут");
    ImGui::TableSetupColumn("Км");
    ImGui::TableSetupColumn("Сезон");
    ImGui::TableSetupColumn("Норматив");
    ImGui::TableSetupColumn("Действие");
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(waybillForm_.details.size()); ++i) {
      const auto& d = waybillForm_.details[i];
      const auto it = routeById.find(d.route_id);
      const std::string name = it != routeById.end() ? it->second.name : "<unknown>";
      const double norm = (it != routeById.end())
                              ? fuel::calcNormativeFuel(d.distance_km,
                                                        d.season == "summer" ? it->second.norm_summer_l100
                                                                             : it->second.norm_winter_l100)
                              : 0.0;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", i + 1);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", d.distance_km);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(d.season.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", norm);
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (IconTextButton(kIconDelete, "Удалить", "wb_del_seg", ImVec2(110, 0))) {
        waybillForm_.details.erase(waybillForm_.details.begin() + i);
        ImGui::PopID();
        break;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  if (IconTextButton(kIconSave, "Сохранить путевой лист", "wb_save", ImVec2(260, 0))) {
    if (waybillForm_.details.empty()) {
      uiStatus_ = "Невозможно сохранить: добавьте хотя бы один участок";
    } else {
      WaybillEntry entry{};
      entry.car_id = waybillForm_.selectedCarId;
      entry.driver_id = waybillForm_.selectedDriverId;
      entry.date = waybillForm_.date.data();
      entry.odometer_start = waybillForm_.odometerStart;
      entry.fuel_start_l = waybillForm_.fuelStart;
      entry.fuel_added_l = waybillForm_.fuelAdded;
      entry.fuel_end_l = waybillForm_.fuelEnd;
      entry.odometer_end = odometerEnd;
      entry.calculated_fuel_l = totalNorm;
      entry.actual_fuel_l = actualFuel;
      entry.variance_l = variance;
      entry.notes = waybillForm_.notes.data();

      for (const auto& d : waybillForm_.details) {
        auto it = routeById.find(d.route_id);
        if (it == routeById.end()) continue;
        WaybillRouteDetail out = d;
        const double n = d.season == "summer" ? it->second.norm_summer_l100 : it->second.norm_winter_l100;
        out.calculated_fuel_l = fuel::calcNormativeFuel(d.distance_km, n);
        entry.details.push_back(out);
      }

      if (db_.addWaybill(entry)) {
        uiStatus_ = "Путевой лист сохранен";
        waybillForm_.details.clear();
        waybillForm_.notes[0] = '\0';
      } else {
        uiStatus_ = "Ошибка сохранения: " + db_.lastError();
      }
    }
  }

  ImGui::Columns(1);

  ImGui::Separator();
  ImGui::TextUnformatted("Р–СѓСЂРЅР°Р» РїСѓС‚РµРІС‹С… Р»РёСЃС‚РѕРІ");
  ImGui::InputText("РЎ РґР°С‚С‹", waybillForm_.filterDateFrom.data(), waybillForm_.filterDateFrom.size());
  ImGui::SameLine();
  ImGui::InputText("РџРѕ РґР°С‚Сѓ", waybillForm_.filterDateTo.data(), waybillForm_.filterDateTo.size());

  const std::string carFilterPreview =
      (waybillForm_.filterCarId != 0 && carNameById.count(waybillForm_.filterCarId) > 0) ? carNameById.at(waybillForm_.filterCarId)
                                                                                           : "Р’СЃРµ Р°РІС‚Рѕ";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ Р°РІС‚Рѕ", carFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ Р°РІС‚Рѕ", waybillForm_.filterCarId == 0)) waybillForm_.filterCarId = 0;
    for (const auto& c : cars) {
      const bool selected = c.id == waybillForm_.filterCarId;
      const std::string label = c.plate_number + " / " + c.brand;
      if (ImGui::Selectable(label.c_str(), selected)) waybillForm_.filterCarId = c.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();

  const std::string driverFilterPreview = (waybillForm_.filterDriverId != 0 && driverNameById.count(waybillForm_.filterDriverId) > 0)
                                              ? driverNameById.at(waybillForm_.filterDriverId)
                                              : "Р’СЃРµ РІРѕРґРёС‚РµР»Рё";
  if (ImGui::BeginCombo("Р¤РёР»СЊС‚СЂ РІРѕРґРёС‚РµР»СЊ", driverFilterPreview.c_str())) {
    if (ImGui::Selectable("Р’СЃРµ РІРѕРґРёС‚РµР»Рё", waybillForm_.filterDriverId == 0)) waybillForm_.filterDriverId = 0;
    for (const auto& d : drivers) {
      const bool selected = d.id == waybillForm_.filterDriverId;
      if (ImGui::Selectable(d.full_name.c_str(), selected)) waybillForm_.filterDriverId = d.id;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto rows = db_.listWaybills();
  bool deletedAny = false;
  if (ImGui::BeginTable("waybill_journal", 10,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 220))) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Р”Р°С‚Р°", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("РђРІС‚Рѕ");
    ImGui::TableSetupColumn("Р’РѕРґРёС‚РµР»СЊ");
    ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("РќРѕСЂРј, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("Р¤Р°РєС‚, Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєР»., Р»", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableSetupColumn("РћС‚РєСЂС‹С‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("РЈРґР°Р»РёС‚СЊ", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableHeadersRow();

    for (const auto& w : rows) {
      if (waybillForm_.filterCarId != 0 && w.car_id != waybillForm_.filterCarId) continue;
      if (waybillForm_.filterDriverId != 0 && w.driver_id != waybillForm_.filterDriverId) continue;

      const std::string dateFrom = waybillForm_.filterDateFrom.data();
      const std::string dateTo = waybillForm_.filterDateTo.data();
      if (!dateFrom.empty() && w.date < dateFrom) continue;
      if (!dateTo.empty() && w.date > dateTo) continue;

      double distance = 0.0;
      for (const auto& d : w.details) distance += d.distance_km;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%lld", static_cast<long long>(w.id));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.date.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(carNameById.count(w.car_id) ? carNameById.at(w.car_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(driverNameById.count(w.driver_id) ? driverNameById.at(w.driver_id).c_str() : "<unknown>");
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", distance);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.calculated_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.actual_fuel_l);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", w.variance_l);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(w.id));
      if (IconTextButton(kIconOpen, "РћС‚РєСЂС‹С‚СЊ", "wb_open", ImVec2(-1, 0))) waybillForm_.selectedWaybillId = w.id;
      ImGui::TableNextColumn();
      if (IconTextButton(kIconDelete, "РЈРґР°Р»РёС‚СЊ", "wb_delete", ImVec2(-1, 0))) {
        if (db_.deleteWaybill(w.id)) {
          uiStatus_ = "РџСѓС‚РµРІРѕР№ Р»РёСЃС‚ СѓРґР°Р»РµРЅ";
          if (waybillForm_.selectedWaybillId == w.id) waybillForm_.selectedWaybillId = 0;
          deletedAny = true;
        } else {
          uiStatus_ = "РћС€РёР±РєР° СѓРґР°Р»РµРЅРёСЏ: " + db_.lastError();
        }
      }
      ImGui::PopID();
      if (deletedAny) break;
    }
    ImGui::EndTable();
  }

  if (waybillForm_.selectedWaybillId != 0) {
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const WaybillEntry& x) { return x.id == waybillForm_.selectedWaybillId; });
    if (selected == rows.end()) {
      waybillForm_.selectedWaybillId = 0;
    } else {
      ImGui::Separator();
      ImGui::Text("РљР°СЂС‚РѕС‡РєР° РїСѓС‚РµРІРѕРіРѕ Р»РёСЃС‚Р° в„–%lld", static_cast<long long>(selected->id));
      ImGui::BulletText("Р”Р°С‚Р°: %s", selected->date.c_str());
      ImGui::BulletText("РђРІС‚Рѕ: %s", carNameById.count(selected->car_id) ? carNameById.at(selected->car_id).c_str() : "<unknown>");
      ImGui::BulletText("Р’РѕРґРёС‚РµР»СЊ: %s",
                        driverNameById.count(selected->driver_id) ? driverNameById.at(selected->driver_id).c_str() : "<unknown>");
      ImGui::BulletText("РћРґРѕРјРµС‚СЂ: %.1f -> %.1f", selected->odometer_start, selected->odometer_end);
      ImGui::BulletText("РќРѕСЂРјР°С‚РёРІ: %.2f Р»; Р¤Р°РєС‚: %.2f Р»; РћС‚РєР»РѕРЅРµРЅРёРµ: %.2f Р»", selected->calculated_fuel_l, selected->actual_fuel_l,
                        selected->variance_l);
      if (!selected->notes.empty()) ImGui::TextWrapped("РџСЂРёРјРµС‡Р°РЅРёРµ: %s", selected->notes.c_str());

      if (ImGui::BeginTable("waybill_details_selected", 4,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                            ImVec2(0, 140))) {
        ImGui::TableSetupColumn("РњР°СЂС€СЂСѓС‚");
        ImGui::TableSetupColumn("РљРј", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("РЎРµР·РѕРЅ", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("РќРѕСЂРјР°С‚РёРІ, Р»", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableHeadersRow();
        for (const auto& d : selected->details) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(routeById.count(d.route_id) ? routeById.at(d.route_id).name.c_str() : "<unknown>");
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.distance_km);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(d.season.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", d.calculated_fuel_l);
        }
        ImGui::EndTable();
      }
    }
  }

  ImGui::EndChild();
}
#endif

}  // namespace waysheet
