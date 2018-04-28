using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Controls;

namespace tracer_gui
{
    public class TextBoxStreamWriter : TextWriter
    {
        private readonly TextBox mOutput;
        private readonly ThreadLocal<string> mCachedContent = new ThreadLocal<string>();

        public TextBoxStreamWriter(TextBox output)
        {
            mOutput = output;
        }

        public override void Write(char value)
        {
            base.Write(value);

            mCachedContent.Value += value;
            if (value != '\n')
                return;

            var cachedContent = mCachedContent.Value;
            mCachedContent.Value = "";

            Application.Current.Dispatcher.BeginInvoke(new Action(() =>
            {
                mOutput.AppendText(cachedContent);
                mOutput.ScrollToEnd();
            }));
        }

        public override Encoding Encoding
        {
            // ReSharper disable once ConvertPropertyToExpressionBody
            get { return Encoding.UTF8; }
        }
    }
}
