using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

class BrandTools {
  static void MakeDarkLogo(string srcPath, string dstPath) {
    using (var src = new Bitmap(srcPath)) {
      var rect = new Rectangle(0, 0, src.Width, src.Height);
      var src32 = src.Clone(rect, PixelFormat.Format32bppArgb);
      var bd = src32.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
      int n = Math.Abs(bd.Stride) * bd.Height;
      byte[] px = new byte[n];
      Marshal.Copy(bd.Scan0, px, 0, n);
      for (int i = 0; i < n; i += 4) {
        byte b = px[i], g = px[i+1], r = px[i+2], a = px[i+3];
        if (a == 0) continue;
        if (b > 140 && b > (r + 40) && r < 120) continue; // bright blue
        if (r > 200 && g > 200 && b > 200) continue; // glint
        if (b >= r && b >= g && r < 90 && g < 100 && b < 160) {
          double t = Math.Min(1.0, b / 90.0);
          px[i+2] = (byte)Math.Min(255, (int)(210 + 30 * (1 - t)));
          px[i+1] = (byte)Math.Min(255, (int)(220 + 25 * (1 - t)));
          px[i]   = (byte)Math.Min(255, (int)(235 + 15 * (1 - t)));
        }
      }
      Marshal.Copy(px, 0, bd.Scan0, n);
      src32.UnlockBits(bd);
      src32.Save(dstPath, ImageFormat.Png);
      src32.Dispose();
    }
  }

  static void WriteIco(string outPath, string[] pngPaths, int[] sizes) {
    using (var fs = new FileStream(outPath, FileMode.Create, FileAccess.Write))
    using (var bw = new BinaryWriter(fs)) {
      int count = pngPaths.Length;
      var images = new byte[count][];
      for (int i = 0; i < count; i++) {
        using (var bmp = new Bitmap(pngPaths[i]))
        using (var sized = new Bitmap(sizes[i], sizes[i], PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(sized))
        using (var ms = new MemoryStream()) {
          g.Clear(Color.Transparent);
          g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
          g.DrawImage(bmp, 0, 0, sizes[i], sizes[i]);
          sized.Save(ms, ImageFormat.Png);
          images[i] = ms.ToArray();
        }
      }
      bw.Write((ushort)0); bw.Write((ushort)1); bw.Write((ushort)count);
      int offset = 6 + 16 * count;
      for (int i = 0; i < count; i++) {
        int d = sizes[i];
        bw.Write((byte)(d >= 256 ? 0 : d));
        bw.Write((byte)(d >= 256 ? 0 : d));
        bw.Write((byte)0); bw.Write((byte)0);
        bw.Write((ushort)1); bw.Write((ushort)32);
        bw.Write((uint)images[i].Length);
        bw.Write((uint)offset);
        offset += images[i].Length;
      }
      for (int i = 0; i < count; i++) bw.Write(images[i]);
    }
  }

  static int Main(string[] args) {
    string root = args[0];
    MakeDarkLogo(Path.Combine(root, "assets", "dabom-logo.png"),
                 Path.Combine(root, "assets", "dabom-logo-dark.png"));
    int[] sizes = new int[] { 16, 32, 48, 256 };
    string[] pngs = new string[sizes.Length];
    for (int i = 0; i < sizes.Length; i++)
      pngs[i] = Path.Combine(root, "assets", "icons", "dabom-icon-" + sizes[i] + ".png");
    WriteIco(Path.Combine(root, "assets", "dabom-16-32-48-256.ico"), pngs, sizes);
    Console.WriteLine("OK dark logo + minimal ico");
    return 0;
  }
}
