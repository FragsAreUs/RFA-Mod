using System.Windows;
using System.Windows.Media.Media3D;

namespace RfaEditor;

/// <summary>
/// The level view's camera, the way game editors move around a level: a position, and the
/// direction it looks (turned left/right and up/down). Everything is in WPF's 3D space, where Z is
/// flipped compared to the game.
/// </summary>
sealed class FlyCamera
{
    public Point3D Position { get; set; }
    public double Yaw { get; set; }    // radians: turning left and right
    public double Pitch { get; set; }  // radians: looking up (+) and down (-)
    public const double DefaultFieldOfView = 60;
    public double FieldOfView { get; set; } = DefaultFieldOfView;  // degrees, side to side (what WPF's camera takes)

    /// <summary>The side-to-side field of view that shows a vertical one (as the game's camera has) in a view of this shape.</summary>
    public static double SideToSide(double vertical, Size view) =>
        view.Width <= 0 || view.Height <= 0 ? DefaultFieldOfView
            : 2 * Math.Atan(Math.Tan(vertical * Math.PI / 360) * view.Width / view.Height) * 180 / Math.PI;

    static readonly Vector3D WorldUp = new(0, 1, 0);

    public Vector3D Forward => new(Math.Cos(Pitch) * Math.Sin(Yaw), Math.Sin(Pitch), Math.Cos(Pitch) * Math.Cos(Yaw));

    public Vector3D Right
    {
        get
        {
            Vector3D right = Vector3D.CrossProduct(Forward, WorldUp);
            right.Normalize();
            return right;
        }
    }

    public Vector3D Up => Vector3D.CrossProduct(Right, Forward);

    /// <summary>Turns the camera: positive yaw turns left, positive pitch looks up.</summary>
    public void Turn(double yaw, double pitch)
    {
        Yaw += yaw;
        Pitch = Math.Clamp(Pitch + pitch, -1.55, 1.55);
    }

    /// <summary>Puts the camera where it sees a point from a distance that shows something this big.</summary>
    public void LookAt(Point3D target, double size)
    {
        Position = target - Forward * Math.Max(4, size * 2.5);
    }

    /// <summary>Looks at all of something from above at an angle.</summary>
    public void Frame(Rect3D bounds)
    {
        if (bounds.IsEmpty) return;
        Yaw = 0.8 * Math.PI;
        Pitch = -0.6;
        var centre = new Point3D(bounds.X + bounds.SizeX / 2, bounds.Y + bounds.SizeY / 2, bounds.Z + bounds.SizeZ / 2);
        double radius = new Vector3D(bounds.SizeX, bounds.SizeY, bounds.SizeZ).Length / 2;
        Position = centre - Forward * Math.Max(10, radius * 1.6);
    }

    public void Apply(PerspectiveCamera camera)
    {
        camera.Position = Position;
        camera.LookDirection = Forward;
        camera.UpDirection = WorldUp;
        camera.FieldOfView = FieldOfView;
        camera.NearPlaneDistance = 0.1;
        camera.FarPlaneDistance = 20000;
    }

    /// <summary>The line from the camera through a point of the view (for picking and dragging).</summary>
    public (Point3D Origin, Vector3D Direction) Ray(Point point, Size view)
    {
        double tanHalf = Math.Tan(FieldOfView * Math.PI / 360);
        double x = (2 * point.X / view.Width - 1) * tanHalf;
        double y = (1 - 2 * point.Y / view.Height) * tanHalf * view.Height / view.Width;
        Vector3D direction = Forward + Right * x + Up * y;
        direction.Normalize();
        return (Position, direction);
    }
}
