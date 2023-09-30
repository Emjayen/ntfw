/*
 * config.h
 *   Build-time constants
 *
 */
#pragma once




// Controls the maximum number of filter chain attachments. As an exclusively
// a 'modifying' type filter, we are guarenteed to be only be inserted once
// per device (NIC, physical or otherwise) and thus there's a reasonable bound.
#define MAX_FILTER_MODULES  8