///
/// \file    sentinel_ds3231.cpp
/// \brief   DS3231 real-time clock driver — translation unit
///
/// \details The \ref sentinel::ds3231 driver is implemented entirely
///          inline in \c sentinel_ds3231.hpp (it is a class template
///          parameterized over the transport type). This translation
///          unit exists so the file appears in the build system's source
///          scan and so future non-template helpers can be added here
///          without re-shaping the build.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 1.0 - Datasheet-derived DS3231 driver — TU shim
///

#include "sentinel_ds3231.hpp"
