///
/// \file    sentinel_bme280.cpp
/// \brief   BME280 temperature/pressure/humidity sensor driver — translation
///          unit
///
/// \details The \ref sentinel::bme280 driver is implemented entirely inline in
///          \c sentinel_bme280.hpp (it is a class template parameterized over
///          the transport type). This translation unit exists so the file
///          appears in the build system's source scan and so future non-
///          template helpers can be added here without re-shaping the build.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Transport-agnostic Bosch BME280 driver — TU shim
///

#include "sentinel_bme280.hpp"
