#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "database.hpp"
#include "form_manager.hpp"
#include "theme_manager.hpp"

namespace waysheet {

class App {
 public:
  App();
  int run();

 private:
  bool initStorage();
  void seedDemoData();
  void showDashboardText() const;
  int runGui();
  void applyModernStyle() const;
  void drawWorkspace();
  void drawDashboard();
  void drawMonthlySummaryTab();
  void drawDbManagerWindow();
  void drawCarsTab();
  void drawDriversTab();
  void drawRoutesTab();
  void drawWaybillTab();
  void pushRecentDb(const std::string& path);
  void loadRecentDbs();
  void saveRecentDbs() const;
  bool createNewDatabase(const std::string& path);
  bool openDatabase(const std::string& path);
  bool saveDatabaseAs(const std::string& path);
  bool recalculateWaybillChainForCar(std::int64_t carId);
  bool exportMonthlyWaybillReportHtml(const std::string& month);

  std::filesystem::path dbPath_;
  Database db_;
  ThemeManager themes_;
  FormManager forms_;

  std::array<char, 512> dbCreatePath_{};
  std::array<char, 512> dbOpenPath_{};
  std::array<char, 512> dbSaveAsPath_{};
  std::string uiStatus_;
  int activeMainTab_{0};
  std::vector<std::string> recentDbs_;
  std::filesystem::path recentDbStore_{"recent_dbs.txt"};

  struct CarFormState {
    std::array<char, 64> plate{};
    std::array<char, 96> brand{};
    float tankVolume{60.0F};
    float initialOdometer{0.0F};
    float initialFuel{0.0F};
    std::array<char, 32> registrationDate{};
    bool active{true};
  };

  struct DriverFormState {
    std::array<char, 128> fullName{};
    std::array<char, 64> phone{};
    std::array<char, 64> license{};
    bool active{true};
  };

  struct RouteFormState {
    std::array<char, 128> name{};
    float normSummer{12.0F};
    float normWinter{14.0F};
  };

  struct WaybillFormState {
    std::array<char, 16> date{};
    float odometerStart{0.0F};
    float fuelStart{0.0F};
    float fuelAdded{0.0F};
    float fuelEnd{0.0F};
    std::array<char, 256> notes{};

    std::int64_t selectedCarId{0};
    std::int64_t selectedDriverId{0};
    std::int64_t selectedRouteId{0};
    float newRouteDistance{0.0F};

    std::vector<WaybillRouteDetail> details;

    std::array<char, 16> filterDateFrom{};
    std::array<char, 16> filterDateTo{};
    std::int64_t filterCarId{0};
    std::int64_t filterDriverId{0};
    std::int64_t selectedWaybillId{0};
  };

  CarFormState carForm_;
  DriverFormState driverForm_;
  RouteFormState routeForm_;
  WaybillFormState waybillForm_;
};

}  // namespace waysheet
