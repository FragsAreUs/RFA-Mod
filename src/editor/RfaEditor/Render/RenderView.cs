using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Vortice.Direct3D11;
using Vortice.Direct3D9;
using Vortice.DXGI;
using D3D9Format = Vortice.Direct3D9.Format;
using DxgiFormat = Vortice.DXGI.Format;

namespace RfaEditor.Render;

/// <summary>
/// One view's pictures: the colour picture Direct3D 11 draws into, the depth picture that keeps
/// near things in front of far ones, and the picture WPF's <see cref="Image"/> shows (through a
/// D3DImage; shared with a Direct3D 9 texture, which is what WPF can take). The colour picture has
/// an alpha channel: where nothing is drawn it stays see-through, so whatever WPF has behind the
/// view (its background) shows.
///
/// WPF takes the shown picture on its own thread, with Direct3D 9, which can't see what Direct3D 11
/// still has to do: drawing straight into the shown picture, WPF sometimes took a frame half drawn
/// (while moving about: parts missing, the sky showing through the ground). So each frame is drawn
/// into a picture of its own, then copied into the shown one while WPF is kept off it, and handed
/// over only once the graphics card has finished the copy.
///
/// Waiting for the card to finish a frame held up the editor (it stuttered while moving about), so
/// a frame is handed over at WPF's next turn to draw (<see cref="Show"/>), when the card has long
/// finished it; only the copy is waited for, which takes the card a fraction of a millisecond.
///
/// Depth is "reversed": 1 at the near plane, falling towards 0 far away, kept as floats. That keeps
/// distant walls from flickering through each other in a level a few kilometres across.
/// </summary>
sealed class RenderView : IDisposable
{
    readonly GpuDevice gpu;
    ID3D11Texture2D? colour, depth, shown;
    ID3D11RenderTargetView? colourView;
    ID3D11Query? drawn, copied;  // each passed once the graphics card has done everything asked before it
    bool waiting;  // a frame is drawn, or being drawn, that WPF hasn't been given yet
    ID3D11DepthStencilView? depthView;
    IDirect3DTexture9? shared9;
    IDirect3DSurface9? surface9;

    /// <summary>The picture to show: put it in an Image's Source.</summary>
    public D3DImage Image { get; } = new();
    public int Width { get; private set; }
    public int Height { get; private set; }

    public RenderView(GpuDevice gpu) => this.gpu = gpu;

    /// <summary>Makes the pictures this size (in pixels); nothing happens if they already are.</summary>
    public void Resize(int width, int height)
    {
        width = Math.Max(1, width);
        height = Math.Max(1, height);
        if (width == Width && height == Height && colour != null) return;
        ReleasePictures();
        Width = width;
        Height = height;

        colour = gpu.Device.CreateTexture2D(new Texture2DDescription(DxgiFormat.B8G8R8A8_UNorm, (uint)width, (uint)height, 1, 1,
            BindFlags.RenderTarget | BindFlags.ShaderResource));
        colourView = gpu.Device.CreateRenderTargetView(colour);
        depth = gpu.Device.CreateTexture2D(new Texture2DDescription(DxgiFormat.D32_Float, (uint)width, (uint)height, 1, 1, BindFlags.DepthStencil));
        depthView = gpu.Device.CreateDepthStencilView(depth);
        shown = gpu.Device.CreateTexture2D(new Texture2DDescription(DxgiFormat.B8G8R8A8_UNorm, (uint)width, (uint)height, 1, 1,
            BindFlags.RenderTarget | BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.Shared));
        drawn ??= gpu.Device.CreateQuery(new QueryDescription(Vortice.Direct3D11.QueryType.Event));
        copied ??= gpu.Device.CreateQuery(new QueryDescription(Vortice.Direct3D11.QueryType.Event));

        // The shown picture, seen by Direct3D 9, for WPF.
        using IDXGIResource resource = shown.QueryInterface<IDXGIResource>();
        IntPtr handle = resource.SharedHandle;
        shared9 = gpu.Device9.CreateTexture((uint)width, (uint)height, 1, Vortice.Direct3D9.Usage.RenderTarget, D3D9Format.A8R8G8B8, Pool.Default, ref handle);
        surface9 = shared9.GetSurfaceLevel(0);
        Image.Lock();
        Image.SetBackBuffer(D3DResourceType.IDirect3DSurface9, surface9.NativePointer, enableSoftwareFallback: true);
        Image.Unlock();
    }

    /// <summary>
    /// Draws a frame: lets <paramref name="first"/> draw into pictures of its own (the lamps'
    /// shadow maps), clears the view's pictures (colour see-through, depth to "far") and lets
    /// <paramref name="draw"/> draw. <see cref="Show"/> hands it to WPF once the card has done it.
    /// </summary>
    public void Draw(Action<ID3D11DeviceContext> draw, Action<ID3D11DeviceContext>? first = null)
    {
        if (colourView == null || depthView == null || drawn == null) return;
        ID3D11DeviceContext context = gpu.Context;
        if (waiting) Show(wait: true);  // the last frame first: its picture is about to be drawn over
        first?.Invoke(context);
        context.OMSetRenderTargets(colourView, depthView);
        context.RSSetViewport(0, 0, Width, Height, 0, 1);
        context.ClearRenderTargetView(colourView, new Vortice.Mathematics.Color4(0, 0, 0, 0));
        context.ClearDepthStencilView(depthView, DepthStencilClearFlags.Depth, 0, 0);
        draw(context);
        context.End(drawn);
        context.Flush();
        waiting = true;
    }

    /// <summary>
    /// Hands the last frame drawn to WPF, if the card has finished it (or, told to wait, once it
    /// has). Call it each time WPF draws.
    /// </summary>
    public void Show(bool wait = false)
    {
        if (!waiting || shown == null || colour == null || drawn == null || copied == null) return;
        ID3D11DeviceContext context = gpu.Context;
        if (!wait && !context.IsDataAvailable(drawn)) return;
        // Lock waits until WPF has taken the last frame and keeps it off the shown picture while
        // the new one is copied in; the copy must be done before WPF is let back on.
        Image.Lock();
        context.CopyResource(shown, colour);
        context.End(copied);
        context.Flush();
        WaitFor(copied);
        // Given with software fallback, the picture also shows when WPF draws without the graphics
        // card (remote desktop, the off-screen tests).
        Image.AddDirtyRect(new Int32Rect(0, 0, Width, Height));
        Image.Unlock();
        waiting = false;
    }

    /// <summary>
    /// Waits for the card to pass a query, by spinning: Windows' sleeps can take 16 ms, which is a
    /// whole frame (and half a second at most, in case the card has gone).
    /// </summary>
    void WaitFor(ID3D11Query query)
    {
        var waited = System.Diagnostics.Stopwatch.StartNew();
        while (!gpu.Context.IsDataAvailable(query) && waited.ElapsedMilliseconds < 500) Thread.SpinWait(64);
    }

    /// <summary>The last frame as a picture (for saving, and the off-screen tests).</summary>
    public BitmapSource ReadPixels()
    {
        if (colour == null) throw new InvalidOperationException("nothing drawn yet");
        var staging = gpu.Device.CreateTexture2D(new Texture2DDescription(DxgiFormat.B8G8R8A8_UNorm, (uint)Width, (uint)Height, 1, 1,
            BindFlags.None, ResourceUsage.Staging, CpuAccessFlags.Read));
        try
        {
            gpu.Context.CopyResource(staging, colour);
            Vortice.Direct3D11.MappedSubresource mapped = gpu.Context.Map(staging, 0, MapMode.Read, Vortice.Direct3D11.MapFlags.None);
            try
            {
                var pixels = new byte[Width * Height * 4];
                for (int y = 0; y < Height; y++)
                    System.Runtime.InteropServices.Marshal.Copy(mapped.DataPointer + (nint)(y * mapped.RowPitch), pixels, y * Width * 4, Width * 4);
                var bitmap = BitmapSource.Create(Width, Height, 96, 96, PixelFormats.Pbgra32, null, pixels, Width * 4);
                bitmap.Freeze();
                return bitmap;
            }
            finally { gpu.Context.Unmap(staging, 0); }
        }
        finally { staging.Dispose(); }
    }

    void ReleasePictures()
    {
        if (surface9 != null)
        {
            Image.Lock();
            Image.SetBackBuffer(D3DResourceType.IDirect3DSurface9, IntPtr.Zero);
            Image.Unlock();
        }
        surface9?.Dispose();
        shared9?.Dispose();
        shown?.Dispose();
        colourView?.Dispose();
        colour?.Dispose();
        depthView?.Dispose();
        depth?.Dispose();
        surface9 = null;
        shared9 = null;
        shown = null;
        waiting = false;
        colourView = null;
        colour = null;
        depthView = null;
        depth = null;
    }

    public void Dispose()
    {
        ReleasePictures();
        drawn?.Dispose();
        copied?.Dispose();
        drawn = copied = null;
    }
}
