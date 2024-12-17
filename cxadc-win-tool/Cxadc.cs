﻿// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cxadc-win-tool - Example tool for using the cxadc-win driver
 *
 * Copyright (C) 2024 Jitterbug
 */

using System;
using System.Buffers.Binary;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Storage.FileSystem;

namespace cxadc_win_tool;

public class Cxadc : IDisposable
{
    public const uint CX_IOCTL_GET_CAPTURE_STATE = 0x800;
    public const uint CX_IOCTL_GET_OUFLOW_COUNT = 0x810;
    public const uint CX_IOCTL_GET_VMUX = 0x821;
    public const uint CX_IOCTL_GET_LEVEL = 0x822;
    public const uint CX_IOCTL_GET_TENBIT = 0x823;
    public const uint CX_IOCTL_GET_SIXDB = 0x824;
    public const uint CX_IOCTL_GET_CENTER_OFFSET = 0x825;
    public const uint CX_IOCTL_GET_BUS_NUMBER = 0x830;
    public const uint CX_IOCTL_GET_DEVICE_ADDRESS = 0x831;
    public const uint CX_IOCTL_RESET_OUFLOW_COUNT = 0x910;
    public const uint CX_IOCTL_SET_VMUX = 0x921;
    public const uint CX_IOCTL_SET_LEVEL = 0x922;
    public const uint CX_IOCTL_SET_TENBIT = 0x923;
    public const uint CX_IOCTL_SET_SIXDB = 0x924;
    public const uint CX_IOCTL_SET_CENTER_OFFSET = 0x925;

    const uint FILE_DEVICE_UNKNOWN = 0x00000022;
    const uint METHOD_BUFFERED = 0;
    const uint FILE_READ_DATA = 0x0001;
    const uint FILE_WRITE_DATA = 0x0002;

    private readonly SafeHandle _handle;
    private bool _disposed = false;

    public Cxadc(string devicePath)
    {
        this._handle = PInvoke.CreateFile(
            devicePath,
            (uint)0x80000000L | (uint)0x40000000L, // R/W
            FILE_SHARE_MODE.FILE_SHARE_READ | FILE_SHARE_MODE.FILE_SHARE_WRITE,
            null,
            FILE_CREATION_DISPOSITION.OPEN_EXISTING,
            0,
            null);

        var err = Marshal.GetLastWin32Error();
        var errStr = new Win32Exception(err).Message;

        if (err != 0)
        {
            throw new Exception($"Unable to open {devicePath}: {errStr}");
        }
    }

    public int Read(Span<byte> buffer)
    {
        uint bytesRead = 0;
        bool ret;

        unsafe
        {
            ret = PInvoke.ReadFile(this._handle, buffer, &bytesRead, null);
        }

        if (!ret)
        {
            var err = Marshal.GetLastWin32Error();
            var errStr = new Win32Exception(err).Message;

            if (err != 0)
            {
                throw new Exception($"Read failed: {errStr}");
            }
        }

        return (int)bytesRead;
    }

    public uint Get(uint code)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(Get(code, sizeof(uint), []));
    }

    public uint Get(uint code, byte[] data)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(Get(code, sizeof(uint), data));
    }

    public byte[] Get(uint code, uint len, byte[] data)
    {
        uint bytesRead = 0;
        byte[] ret;

        unsafe
        {
            var outBuffer = Marshal.AllocHGlobal((int)len);
            var inBuffer = Marshal.AllocHGlobal(data.Length);
            Marshal.Copy(data, 0, inBuffer, data.Length);

            PInvoke.DeviceIoControl(
                this._handle,
                GetCtlCode(code, FILE_READ_DATA),
                inBuffer.ToPointer(),
                (uint)data.Length,
                outBuffer.ToPointer(),
                len,
                &bytesRead,
                null);

            var err = Marshal.GetLastWin32Error();
            var errStr = new Win32Exception(err).Message;

            if (err != 0)
            {
                throw new Exception($"Get failed: {errStr}");
            }

            ret = new byte[bytesRead];
            Marshal.Copy(outBuffer, ret, 0, (int)bytesRead);
        }

        return ret;
    }

    public void Set(uint code, uint value)
    {
        var data = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(data, value);

        Set(code, data);
    }

    public void Set(uint code, byte[] data)
    {
        uint bytesRead = 0;

        unsafe
        {
            var inBuffer = Marshal.AllocHGlobal(data.Length);
            Marshal.Copy(data, 0, inBuffer, data.Length);

            PInvoke.DeviceIoControl(
                this._handle,
                GetCtlCode(code, FILE_WRITE_DATA),
                inBuffer.ToPointer(),
                (uint)data.Length,
                null,
                0,
                &bytesRead,
                null);

            var err = Marshal.GetLastWin32Error();
            var errStr = new Win32Exception(err).Message;

            if (err != 0)
            {
                throw new Exception($"Set failed: {errStr}");
            }
        }
    }

    ~Cxadc() => Dispose();

    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (this._disposed)
        {
            return;
        }

        if (disposing)
        {
            if (!this._handle.IsClosed)
            {
                this._handle.Close();
            }
        }

        this._disposed = true;
    }

    public static uint GetCtlCode(uint function, uint method)
    {
        return FILE_DEVICE_UNKNOWN << 16 | method << 14 | function << 2 | METHOD_BUFFERED;
    }
}
