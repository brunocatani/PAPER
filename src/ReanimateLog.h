#pragma once

#include "support/Logger.h"

#define REANIMATE_LOG_TRACE(category, ...) \
    ::rock_reanimate::logger::trace("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
#define REANIMATE_LOG_DEBUG(category, ...) \
    ::rock_reanimate::logger::debug("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
#define REANIMATE_LOG_INFO(category, ...) \
    ::rock_reanimate::logger::info("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
#define REANIMATE_LOG_WARN(category, ...) \
    ::rock_reanimate::logger::warn("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
#define REANIMATE_LOG_ERROR(category, ...) \
    ::rock_reanimate::logger::error("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
#define REANIMATE_LOG_CRITICAL(category, ...) \
    ::rock_reanimate::logger::critical("[ROCK_Reanimate::" #category "] " __VA_ARGS__)
