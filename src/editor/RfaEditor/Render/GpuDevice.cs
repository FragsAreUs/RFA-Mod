using System.Runtime.InteropServices;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.Direct3D9;
using Vortice.DXGI;

namespace RfaEditor.Render;

/// <summary>
/// The editor's Direct3D 11 device, shared by every view that draws with it, and the Direct3D 9
/// device WPF needs to show what it draws. WPF itself draws with Direct3D 9: a picture drawn by
/// Direct3D 11 is shared with a Direct3D 9 texture, which WPF's D3DImage shows like any other
/// picture (see <see cref="RenderView"/>). Both devices must be on the graphics chip WPF uses, so
/// the Direct3D 11 one is made on the chip of Direct3D 9's main adapter (on a PC with a graphics
/// card and a processor with graphics of its own, the one the main monitor is plugged into).
/// </summary>
sealed class GpuDevice : IDisposable
{
    public ID3D11Device Device { get; }
    public ID3D11DeviceContext Context { get; }
    public IDirect3DDevice9Ex Device9 { get; }
    /// <summary>The graphics chip's name, for the log.</summary>
    public string Adapter { get; }

    readonly IDirect3D9Ex direct3D9;

    static GpuDevice? shared;
    static string? failure;

    /// <summary>The device, made when first asked for; null if this PC can't (see <see cref="Failure"/>).</summary>
    public static GpuDevice? Shared
    {
        get
        {
            if (shared != null || failure != null) return shared;
            try { shared = new GpuDevice(); }
            catch (Exception ex) { failure = ex.Message; }
            return shared;
        }
    }

    /// <summary>Why the device couldn't be made, if it couldn't.</summary>
    public static string? Failure => failure;

    GpuDevice()
    {
        direct3D9 = D3D9.Direct3DCreate9Ex();
        long luid = direct3D9.GetAdapterLuid(0);
        var present = new Vortice.Direct3D9.PresentParameters
        {
            Windowed = true,
            SwapEffect = Vortice.Direct3D9.SwapEffect.Discard,
            DeviceWindowHandle = GetDesktopWindow(),
            PresentationInterval = PresentInterval.Default,
        };
        Device9 = direct3D9.CreateDeviceEx(0, DeviceType.Hardware, IntPtr.Zero,
                                          CreateFlags.HardwareVertexProcessing | CreateFlags.Multithreaded | CreateFlags.FpuPreserve, present);

        // The Direct3D 11 adapter with the same id as Direct3D 9's main one.
        using IDXGIFactory1 factory = DXGI.CreateDXGIFactory1<IDXGIFactory1>();
        IDXGIAdapter1? chosen = null;
        for (uint i = 0; factory.EnumAdapters1(i, out IDXGIAdapter1 adapter).Success; i++)
        {
            if (chosen == null && (long)adapter.Description1.Luid == luid) chosen = adapter;
            else adapter.Dispose();
        }
        if (chosen == null) throw new InvalidOperationException("no Direct3D 11 adapter matches the one WPF draws with");
        Adapter = chosen.Description1.Description;

        FeatureLevel[] levels = [FeatureLevel.Level_11_1, FeatureLevel.Level_11_0, FeatureLevel.Level_10_1, FeatureLevel.Level_10_0];
        D3D11.D3D11CreateDevice(chosen, DriverType.Unknown, DeviceCreationFlags.BgraSupport, levels,
                                out ID3D11Device device, out ID3D11DeviceContext context).CheckError();
        chosen.Dispose();
        Device = device;
        Context = context;
    }

    public void Dispose()
    {
        Context.Dispose();
        Device.Dispose();
        Device9.Dispose();
        direct3D9.Dispose();
    }

    [DllImport("user32.dll")]
    static extern IntPtr GetDesktopWindow();
}
