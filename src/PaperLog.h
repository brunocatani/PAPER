#pragma once

#include "support/Logger.h"

#define PAPER_LOG_TRACE(category, ...) \
    ::paper::logger::trace("[PAPER::" #category "] " __VA_ARGS__)
#define PAPER_LOG_DEBUG(category, ...) \
    ::paper::logger::debug("[PAPER::" #category "] " __VA_ARGS__)
#define PAPER_LOG_INFO(category, ...) \
    ::paper::logger::info("[PAPER::" #category "] " __VA_ARGS__)
#define PAPER_LOG_WARN(category, ...) \
    ::paper::logger::warn("[PAPER::" #category "] " __VA_ARGS__)
#define PAPER_LOG_ERROR(category, ...) \
    ::paper::logger::error("[PAPER::" #category "] " __VA_ARGS__)
#define PAPER_LOG_CRITICAL(category, ...) \
    ::paper::logger::critical("[PAPER::" #category "] " __VA_ARGS__)
