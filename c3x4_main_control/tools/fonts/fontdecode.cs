using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace XTEinkTools
{
    /// <summary>
    /// 阅星曈字体二进制文件的数据结构，提供对每个字形像素层的访问
    /// </summary>
    public class XTEinkFontBinary
    {
        private byte[] fontbin;
        private int width;
        private int height;
        private int widthByte;
        private int charByte;
        private const int totalChar = 0x10000;
        public int Width { get => width; }
        public int Height { get => height; }

        /// <summary>
        /// 以指定的宽高在内存里新建一个二进制文件
        /// </summary>
        /// <param name="width"></param>
        /// <param name="height"></param>
        public XTEinkFontBinary(int width, int height)
        {
            this.width = width;
            this.height = height;
            this.widthByte = width / 8;
            if (width % 8 != 0)
            {
                widthByte++;
            }
            charByte = widthByte * height;
            fontbin = new byte[charByte * totalChar];
        }
        /// <summary>
        /// 将输入的字体名称包装成正确的文件名
        /// </summary>
        /// <param name="title">字体名称</param>
        /// <returns></returns>
        public string GetSuggestedFileName(string title)
        {
            return $"{title} {width}×{height}.bin";
        }

        /// <summary>
        /// 从指定的二进制流加载字体数据
        /// </summary>
        /// <param name="stream"></param>
        public void loadFromFile(Stream stream)
        {
            int pos = 0;
            int len = 0;
            byte[] buf = new byte[1024];
            while ((len = stream.Read(buf, 0, buf.Length)) > 0)
            {
                Array.Copy(buf, 0, fontbin, pos, len);
                pos += len;
            }
        }

        /// <summary>
        /// 将内存中的字体二进制保存到流
        /// </summary>
        /// <param name="stream"></param>
        public void saveToFile(Stream stream)
        {
            stream.Write(fontbin, 0, fontbin.Length);
        }
        private int GetFirstByte(int charCode)
        {
            return charCode * charByte;
        }

        /// <summary>
        /// 获取某个字形的像素
        /// </summary>
        /// <param name="charCode">Unicode编码</param>
        /// <param name="x">横坐标</param>
        /// <param name="y">纵坐标</param>
        /// <returns>该像素是否点亮</returns>
        public bool GetPixel(int charCode, int x, int y)
        {
            var pos = GetPixelOffset(charCode, x, y);
            return GetBitAt(fontbin[pos.index], pos.pos);
        }

        /// <summary>
        /// 设置某个自行的像素
        /// </summary>
        /// <param name="charCode">Unicode编码</param>
        /// <param name="x">横坐标</param>
        /// <param name="y">纵坐标</param>
        /// <param name="value">该像素是否点亮</param>
        public void SetPixel(int charCode, int x, int y, bool value)
        {
            var pos = GetPixelOffset(charCode, x, y);
            fontbin[pos.index] = SetBitAt(fontbin[pos.index], pos.pos, value);
        }

        private (int index, int pos) GetPixelOffset(int charCode, int x, int y)
        {
            var fb = GetFirstByte(charCode);
            fb += y * widthByte;
            fb += x / 8;
            return (fb, x % 8);
        }

        private byte[] bitMask = new byte[] { 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1 };
        private bool GetBitAt(byte b, int p)
        {
            return (b & bitMask[p]) != 0;
        }
        private byte SetBitAt(byte b, int p, bool value)
        {
            if (value)
            {
                return (byte)(b | bitMask[p]);
            }
            else
            {
                return (byte)(b & (~bitMask[p]));
            }

        }



    }
}
