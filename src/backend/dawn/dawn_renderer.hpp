// Dawn GPU renderer (ADR-009) — internal implementation header.
//
// The public API lives in <continui/backend_dawn.hpp>; this header forwards
// to it so the backend's private sources share one definition (single point
// of truth). Include it instead of the public header only inside src/.
#pragma once

#include <continui/backend_dawn.hpp>
