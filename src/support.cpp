#include <iterator>
#include <hardware/dma.h>
#include "support.h"

namespace support
{

    void init_out_pin(uint pin, bool value)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, value);
    }

    void init_in_pin(uint pin)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
    }

    void setup_dma_circular_read_config(int ch, int ctrl_ch, uint32_t dreq, io_wo_32 *write_adr, uint32_t count, bool irq_quiet)
    {
        // ctrl
        auto dma_config = dma_channel_get_default_config(ctrl_ch);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, false);
        channel_config_set_write_increment(&dma_config, false);
        dma_channel_configure(
            ctrl_ch, &dma_config, &dma_hw->ch[ch].al3_read_addr_trig, nullptr, 0, false);

        // read
        dma_config = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_write_increment(&dma_config, false);
        channel_config_set_dreq(&dma_config, dreq);
        channel_config_set_chain_to(&dma_config, ctrl_ch);
        channel_config_set_irq_quiet(&dma_config, irq_quiet);
        dma_channel_configure(ch, &dma_config, write_adr, nullptr, count, false);
    }

    void setup_dma_circular_write_config(int ch, int ctrl_ch, uint32_t dreq, io_ro_32 *read_adr, uint32_t count, bool irq_quiet)
    {
        // ctrl
        auto dma_config = dma_channel_get_default_config(ctrl_ch);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, false);
        channel_config_set_write_increment(&dma_config, false);
        dma_channel_configure(
            ctrl_ch, &dma_config, &dma_hw->ch[ch].al2_write_addr_trig, nullptr, 0, false);

        // write
        dma_config = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, false);
        channel_config_set_write_increment(&dma_config, true);
        channel_config_set_dreq(&dma_config, dreq);
        channel_config_set_chain_to(&dma_config, ctrl_ch);
        channel_config_set_irq_quiet(&dma_config, irq_quiet);
        dma_channel_configure(ch, &dma_config, nullptr, read_adr, count, false);
    }

    void abort_dma_chainning_channles(uint32_t ch1, uint32_t ch2)
    {
        hw_clear_bits(&dma_hw->ch[ch1].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
        hw_clear_bits(&dma_hw->ch[ch2].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);

        dma_hw->abort = (1 << ch1) | (1 << ch2);
        while (dma_hw->abort)
            tight_loop_contents();
        while (dma_hw->ch[ch1].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS)
            tight_loop_contents();
        while (dma_hw->ch[ch2].al1_ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS)
            tight_loop_contents();

        hw_set_bits(&dma_hw->ch[ch1].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
        hw_set_bits(&dma_hw->ch[ch2].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    }

}