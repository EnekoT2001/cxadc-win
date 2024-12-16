// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cxadc-win - CX2388x ADC DMA driver for Windows
 *
 * Copyright (C) 2024 Jitterbug
 *
 * Based on the Linux version created by
 * Copyright (C) 2005-2007 Hew How Chee <how_chee@yahoo.com>
 * Copyright (C) 2013-2015 Chad Page <Chad.Page@gmail.com>
 * Copyright (C) 2019-2023 Adam Sampson <ats@offog.org>
 * Copyright (C) 2020-2022 Tony Anderson <tandersn@cs.washington.edu>
 */

#include "precomp.h"
#include "cx2388x.tmh"

#include "cx2388x.h"

__inline
ULONG cx_read(
    _Inout_ PDEVICE_CONTEXT dev_ctx,
    _In_ ULONG off
)
{
    return READ_REGISTER_ULONG(&dev_ctx->mmio[off >> 2]);
}

__inline
VOID cx_write(
    _Inout_ PDEVICE_CONTEXT dev_ctx,
    _In_ ULONG off,
    _In_ ULONG val
)
{
    WRITE_REGISTER_ULONG(&dev_ctx->mmio[off >> 2], val);
}

__inline
VOID cx_write_buf8(
    _Inout_ PDEVICE_CONTEXT dev_ctx,
    _In_ ULONG off,
    _In_ PUCHAR buf,
    _In_ ULONG count
)
{
    WRITE_REGISTER_BUFFER_UCHAR((PUCHAR)&dev_ctx->mmio[off >> 2], buf, count);
}

NTSTATUS cx_init(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;

    // the following values & comments are from the Linux driver
    // I don't fully understand what each value is doing, nor if/why they are required

    cx_init_cdt(dev_ctx);
    cx_init_risc(dev_ctx);
    cx_init_cmds(dev_ctx);

    // clear interrupt
    cx_write(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_STATUS_ADDR, cx_read(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_STATUS_ADDR));

    // allow full range
    cx_write(dev_ctx, CX_VIDEO_OUTPUT_CONTROL_ADDR,
        (CX_VIDEO_OUTPUT_CONTROL) {
            .hsfmt = 1,
            .hactext = 1,
            .range = 1
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_CONTRAST_BRIGHTNESS_ADDR,
        (CX_VIDEO_CONTRAST_BRIGHTNESS) {
            .cntrst = 0xFF
        }.dword);

    // no of byte transferred from peripheral to fifo
    // if fifo buffer < this, it will still transfer this no of byte
    // must be multiple of 8, if not go haywire?
    cx_write(dev_ctx, CX_VIDEO_VBI_PACKET_SIZE_DELAY_ADDR,
        (CX_VIDEO_VBI_PACKET_SIZE_DELAY) {
            .vbi_v_del = 2,
            .frm_size = CX_CDT_BUF_LEN
        }.dword);

    // raw mode & byte swap << 8 (3 << 8 = swap)
    cx_write(dev_ctx, CX_VIDEO_COLOR_FORMAT_CONTROL_ADDR,
        (CX_VIDEO_COLOR_FORMAT_CONTROL) {
            .color_even = 0xE,
            .color_odd = 0xE,
        }.dword);

    cx_set_tenbit(dev_ctx);

    // power down audio and chroma DAC+ADC
    cx_write(dev_ctx, CX_MISC_AFECFG_ADDR,
        (CX_MISC_AFECFG) {
            .bg_pwrdn = 1,
            .dac_pwrdn = 1
        }.dword);

    // run risc
    cx_write(dev_ctx, CX_DMAC_DEVICE_CONTROL_2_ADDR,
        (CX_DMAC_DEVICE_CONTROL_2) {
            .run_risc = 1
        }.dword);

    // enable fifo and risc
    cx_write(dev_ctx, CX_VIDEO_IPB_DMA_CONTROL_ADDR,
        (CX_VIDEO_IPB_DMA_CONTROL) {
            .vbi_fifo_en = 1,
            .vbi_risc_en = 1
        }.dword);

    // set SRC to 8xfsc
    cx_write(dev_ctx, CX_VIDEO_SAMPLE_RATE_CONVERSION_ADDR,
        (CX_VIDEO_SAMPLE_RATE_CONVERSION) {
            .src_reg_val = 0x20000
        }.dword);
    
    // set PLL to 1:1
    cx_write(dev_ctx, CX_VIDEO_PLL_ADDR,
        (CX_VIDEO_PLL) {
            .pll_int = 0x10,
            .pll_dds = 1
        }.dword);

    // set vbi agc
    // set back porch sample delay & sync sample delay to max
    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_SLICER_ADDR,
        (CX_VIDEO_AGC_SYNC_SLICER) {
            .sync_sam_dly = 0xFF,
            .bp_sam_dly = 0xFF
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_CONTROL_ADDR,
        (CX_VIDEO_AGC_CONTROL) {
            .intrvl_cnt_val = 0xFFF,
            .bp_ref = 0x100,
            .bp_ref_sel = 1,
            .agc_vbi_en = 0,
            .clamp_vbi_en = 0
        }.dword);

    cx_set_level(dev_ctx);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_1_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_1) {
            .trk_sat_val = 0x0F,
            .trk_mode_thr = 0x1C0
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_2_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_2) {
            .acq_sat_val = 0xF,
            .acq_mode_thr = 0x20
        }.dword);

    cx_set_center_offset(dev_ctx);

    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_1_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_1) {
            .trk_agc_sat_val = 7,
            .trk_agc_core_th_val = 0xE,
            .trk_agc_mode_th = 0xE0
        }.dword);


    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_2_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_2) {
            .acq_agc_sat_val = 0xF,
            .acq_gain_val = 2,
            .acq_agc_mode_th = 0x20
        }.dword);

    // set gain of agc but not offset
    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_3_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_3) {
            .acc_inc_val = 0x50,
            .acc_max_val = 0x28,
            .acc_min_val = 0x28
        }.dword);

    // disable PLL adjust (stabilizes output when video is detected by chip)
    CX_VIDEO_PLL_ADJUST pll_adjust =
    {
        .dword = cx_read(dev_ctx, CX_VIDEO_PLL_ADJUST_ADDR)
    };

    pll_adjust.pll_adj_en = 0;
    cx_write(dev_ctx, CX_VIDEO_PLL_ADJUST_ADDR, pll_adjust.dword);

    // i2c sda/scl set to high and use software control
    cx_write(dev_ctx, CX_I2C_DATA_CONTROL_ADDR,
        (CX_I2C_DATA_CONTROL) {
            .sda = 1,
            .scl = 1
        }.dword);

    cx_write(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_MASK_ADDR,
        (CX_DMAC_VIDEO_INTERRUPT) {
            .vbi_riscl1 = 1,
            .vbi_riscl2 = 1,
            .vbif_of = 1,
            .vbi_sync = 1,
            .opc_err = 1
        }.dword);

    return status;
}

NTSTATUS cx_init_cdt(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG cdt_ptr = CX_SRAM_CDT_BASE;
    ULONG buf_ptr = CX_SRAM_CDT_BUF_BASE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_GENERAL, "init cdt table (%d * %d)",
        CX_CDT_BUF_COUNT, CX_CDT_BUF_LEN);

    // set cluster buffer location
    for (ULONG i = 0; i < CX_CDT_BUF_COUNT; i++)
    {
        cx_write_buf8(dev_ctx, cdt_ptr,
            (CX_CDT_DESCRIPTOR) {
                .buffer_ptr = buf_ptr
            }.data,
            sizeof(CX_CDT_DESCRIPTOR));

        cdt_ptr += sizeof(CX_CDT_DESCRIPTOR);
        buf_ptr += CX_CDT_BUF_LEN;
    }

    // size of one buffer - 1
    cx_write(dev_ctx, CX_DMAC_DMA_CNT1_ADDR,
        (CX_DMAC_DMA_CNT1) {
            .dma_cnt1 = CX_CDT_BUF_LEN / 8 - 1
        }.dword);

    // ptr to cdt
    cx_write(dev_ctx, CX_DMAC_DMA_PTR2_ADDR,
        (CX_DMAC_DMA_PTR2) {
            .dma_ptr2 = CX_SRAM_CDT_BASE >> 2
        }.dword);

    // size of cdt
    cx_write(dev_ctx, CX_DMAC_DMA_CNT2_ADDR,
        (CX_DMAC_DMA_CNT2) {
            .dma_cnt2 = CX_CDT_BUF_COUNT * 2
        }.dword);

    return status;
}

NTSTATUS cx_init_risc(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PCX_RISC_INSTRUCTIONS dma_instr_ptr = (PCX_RISC_INSTRUCTIONS)dev_ctx->dma_risc_instr.va;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_GENERAL, "dma phys addr %08X", dev_ctx->dma_risc_instr.la.LowPart);

    // the following comments are from the Linux driver, as they explain the logic sufficiently

    // The RISC program is just a long sequence of WRITEs that fill each DMA page in
    // sequence. It begins with a SYNC and ends with a JUMP back to the first WRITE.

    dma_instr_ptr->sync_instr = (CX_RISC_INSTR_SYNC)
    {
        .opcode = CX_RISC_INSTR_SYNC_OPCODE,
        .cnt_ctl = 3,
    };

    for (ULONG page_idx = 0; page_idx < CX_VBI_BUF_COUNT; page_idx++)
    {
        ULONG dma_page_addr = dev_ctx->dma_risc_page[page_idx].la.LowPart;

        // Each WRITE is CX_CDT_BUF_LEN bytes so each DMA page requires
        //  n = (PAGE_SIZE / CX_CDT_BUF_LEN) WRITEs to fill it.

        // Generate n WRITEs.
        for (ULONG write_idx = 0; write_idx < (PAGE_SIZE / CX_CDT_BUF_LEN); write_idx++)
        {
            PCX_RISC_INSTR_WRITE write_instr = &dma_instr_ptr->write_instr[(page_idx * 2) + write_idx];

            *write_instr = (CX_RISC_INSTR_WRITE)
            {
                .opcode = CX_RISC_INSTR_WRITE_OPCODE,
                .sol = 1,
                .eol = 1,
                .byte_count = CX_CDT_BUF_LEN,
                .pci_target_address = dma_page_addr
            };

            dma_page_addr += CX_CDT_BUF_LEN;

            if (write_idx == (PAGE_SIZE / CX_CDT_BUF_LEN) - 1)
            {
                // always increment final write 
                write_instr->cnt_ctl = 1;

                //  reset counter on last page
                if (page_idx == (CX_VBI_BUF_COUNT - 1))
                {
                    write_instr->cnt_ctl = 3;
    }

                // trigger IRQ1
                if (((page_idx + 1) % CX_IRQ_PERIOD_IN_PAGES) == 0)
                {
                    write_instr->irq1 = 1;
                }
            }
        }
    }

    // Jump back to first WRITE (+4 skips the SYNC command.)
    dma_instr_ptr->jump_instr = (CX_RISC_INSTR_JUMP)
    {
        .opcode = CX_RISC_INSTR_JUMP_OPCODE,
        .jump_address = dev_ctx->dma_risc_instr.la.LowPart + 4
    };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_GENERAL, "filled risc instr dma, total size %lu kbyte",
        sizeof(CX_RISC_INSTRUCTIONS) / 1024);

    return status;
}

NTSTATUS cx_init_cmds(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;

    // init sram
    cx_write_buf8(dev_ctx, CX_SRAM_CMDS_BASE,
        (CX_CMDS) {
            .initial_risc_addr = dev_ctx->dma_risc_instr.la.LowPart,
            .cdt_base = CX_SRAM_CDT_BASE,
            .cdt_size = CX_CDT_BUF_COUNT * 2,
            .risc_base = CX_SRAM_RISC_QUEUE_BASE,
            .risc_size = 0x40
        }.data,
        sizeof(CX_CMDS));

    return status;
}

NTSTATUS cx_disable(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;

    // setting all bits to 0/1 so just write entire dword

    // turn off pci interrupt
    cx_write(dev_ctx, CX_MISC_PCI_INTERRUPT_MASK_ADDR, 0);

    // turn off interrupt
    cx_write(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_MASK_ADDR, 0);
    cx_write(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_STATUS_ADDR, 0xFFFFFFFF);

    // disable fifo and risc
    cx_write(dev_ctx, CX_VIDEO_IPB_DMA_CONTROL_ADDR, 0);

    // disable risc
    cx_write(dev_ctx, CX_DMAC_DEVICE_CONTROL_2_ADDR, 0);

    return status;
}

NTSTATUS cx_reset(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    NTSTATUS status = STATUS_SUCCESS;

    // set agc registers back to default values
    cx_write(dev_ctx, CX_VIDEO_AGC_CONTROL_ADDR,
        (CX_VIDEO_AGC_CONTROL) {
            .intrvl_cnt_val = 0x555,
            .bp_ref = 0xE0
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_SLICER_ADDR,
        (CX_VIDEO_AGC_SYNC_SLICER) {
            .sync_sam_dly = 0x1C,
            .bp_sam_dly = 0x60,
            .mm_multi = 4,
            .std_slice_en = 1,
            .sam_slice_en = 1,
            .dly_upd_en = 1
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_1_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_1) {
            .trk_sat_val = 0xF,
            .trk_mode_thr = 0x1C0
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_2_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_2) {
            .acq_sat_val = 0x3F,
            .acq_g_val = 1,
            .acq_mode_thr = 0x20
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_3_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_3) {
            .acc_max = 0x40,
            .acc_min = 0xE0,
            .low_stip_th = 0x1E48
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_1_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_1) {
            .trk_agc_sat_val = 7,
            .trk_agc_core_th_val = 0xE,
            .trk_agc_mode_th = 0xE0
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_2_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_2) {
            .acq_agc_sat_val = 0xF,
            .acq_gain_val = 2,
            .acq_agc_mode_th = 0x20
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_3_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_3) {
            .acc_inc_val = 0xC0,
            .acc_max_val = 0x38,
            .acc_min_val = 0x28
        }.dword);

    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_4_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_4) {
            .high_acc_val = 0x34,
            .low_acc_val = 0x2C,
            .init_vga_val = 0xA,
            .vga_en = 1,
            .slice_ref_en = 1
        }.dword);

    return status;
}

BOOLEAN cx_evt_isr(
    _In_ WDFINTERRUPT intr,
    _In_ ULONG msg_id
)
{
    UNREFERENCED_PARAMETER(intr);
    UNREFERENCED_PARAMETER(msg_id);

    PDEVICE_CONTEXT dev_ctx = cx_device_get_ctx(WdfInterruptGetDevice(intr));

    CX_DMAC_VIDEO_INTERRUPT mstat =
    {
        .dword = cx_read(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_MSTATUS_ADDR)
    };

    if (!mstat.vbi_riscl1 && mstat.dword)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_GENERAL, "intr stat 0x%0X masked 0x%0X",
            cx_read(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_STATUS_ADDR),
            mstat.dword);
    }

    if (!mstat.dword) {
        return FALSE;
    }

    if (mstat.vbi_riscl1)
    {
        // comment from the Linux driver
        // NB: CX_VBI_GP_CNT is not guaranteed to be in-sync with resident pages.
        // i.e. we can get gp_cnt == 1 but the first page may not yet have been transferred
        // to main memory. on the other hand, if an interrupt has occurred, we are guaranteed to have the page
        // in main memory. so we only retrieve CX_VBI_GP_CNT after an interrupt has occurred and then round
        // it down to the last page that we know should have triggered an interrupt.
        ULONG gp_cnt = cx_read(dev_ctx, CX_VIDEO_VBI_GP_COUNTER_ADDR);
        gp_cnt &= ~(CX_IRQ_PERIOD_IN_PAGES - 1);
        dev_ctx->gp_cnt = gp_cnt;
        KeSetEvent(&dev_ctx->isr_event, IO_NO_INCREMENT, FALSE);
    }

    cx_write(dev_ctx, CX_DMAC_VIDEO_INTERRUPT_STATUS_ADDR, mstat.dword);

    return TRUE;
}

VOID cx_start_capture(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_MISC_PCI_INTERRUPT_MASK_ADDR, 1);
}

VOID cx_stop_capture(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_MISC_PCI_INTERRUPT_MASK_ADDR, 0);
}

VOID cx_set_vmux(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_VIDEO_INPUT_FORMAT_ADDR,
        (CX_VIDEO_INPUT_FORMAT) {
            .fmt = 1,
            .svid = 1,
            .agcen = 1,
            .yadc_sel = dev_ctx->attrs.vmux,
            .svid_c_sel = 1,
        }.dword);
}

VOID cx_set_level(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_VIDEO_AGC_GAIN_ADJUST_4_ADDR,
        (CX_VIDEO_AGC_GAIN_ADJUST_4) {
            .high_acc_val = 0x00,
            .low_acc_val = 0xFF,
            .init_vga_val = dev_ctx->attrs.level,
            .vga_en = 0x00,
            .slice_ref_en = 0x00,
            .init_6db_val = dev_ctx->attrs.sixdb
        }.dword);
}

VOID cx_set_tenbit(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_VIDEO_CAPTURE_CONTROL_ADDR,
        (CX_VIDEO_CAPTURE_CONTROL) {
            .capture_even = 1,
            .capture_odd = 1,
            .raw16 = dev_ctx->attrs.tenbit,
            .cap_raw_all = 1
        }.dword);
}

VOID cx_set_center_offset(
    _Inout_ PDEVICE_CONTEXT dev_ctx
)
{
    cx_write(dev_ctx, CX_VIDEO_AGC_SYNC_TIP_ADJUST_3_ADDR,
        (CX_VIDEO_AGC_SYNC_TIP_ADJUST_3) {
            .acc_max = dev_ctx->attrs.center_offset,
            .acc_min = 0xFF,
            .low_stip_th = 0x1E48
        }.dword);
}
