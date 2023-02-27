#pragma once


namespace streaming
{

void test();

void init();

void set_rx_format(uint32_t sample_frequency, uint32_t bits);
void push_rx_data(size_t (*fn)(uint8_t*, size_t), size_t data_size);
void close_rx();
void get_rx_buffer_status(uint32_t& left, uint32_t& max_size);

void set_tx_format(uint32_t sample_frequency, uint32_t bits);
size_t pop_tx_data(size_t (*fn)(const uint8_t*, const uint8_t*));
void close_tx();

void set_spdif_in_volume(uint8_t value);
void set_line_in_volume(uint8_t value);
uint8_t get_spdif_in_volume();
uint8_t get_line_in_volume();

void print_debug_stats();

}