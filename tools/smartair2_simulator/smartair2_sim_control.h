#pragma once

#include "smartair2_packet.h"

void smartair2_control_init(esphome::haier::smartair2_protocol::HaierPacketControl &state);

void smartair2_control_tick(esphome::haier::smartair2_protocol::HaierPacketControl &state);

void smartair2_control_note_change(const char *source, const char *field, unsigned int old_value, unsigned int new_value);

void smartair2_control_note_group_change(unsigned int byte_index, unsigned int old_value, unsigned int new_value);
