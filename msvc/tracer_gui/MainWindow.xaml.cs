using System;
using System.Diagnostics;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;
using tracer_wrapper;

namespace tracer_gui
{
    public partial class MainWindow : Window
    {
        private class ProcessSelectionItem
        {
            public readonly Process Process;

            public ProcessSelectionItem(Process p)
            {
                Process = p;
            }

            public override string ToString()
            {
                return $"{Process.Id} - {Process.ProcessName}";
            }
        }

        private class UiTraceNode : TreeViewItem
        {
            public readonly UiTraceNode ParentNode;
            public readonly tracer_wrapper.TracedInstruction TracedInstruction;

            public UiTraceNode(UiTraceNode parent, tracer_wrapper.TracedInstruction inst)
            {
                ParentNode = parent;
                TracedInstruction = inst;
                Header = $"{TracerApi.DecodeAndFormatInstruction(inst.BranchSource)}";

                ToolTip = 
$@"BranchSource: 0x{inst.BranchSource.ToUInt64():X8}
BranchTarget: 0x{inst.BranchTarget.ToUInt64():X8}

EAX: 0x{inst.RegisterSet.Eax.ToUInt64():X8}
EBX: 0x{inst.RegisterSet.Ebx.ToUInt64():X8}
ECX: 0x{inst.RegisterSet.Ecx.ToUInt64():X8}
EDX: 0x{inst.RegisterSet.Edx.ToUInt64():X8}
ESI: 0x{inst.RegisterSet.Esi.ToUInt64():X8}
EDI: 0x{inst.RegisterSet.Edi.ToUInt64():X8}
EBP: 0x{inst.RegisterSet.Ebp.ToUInt64():X8}
ESP: 0x{inst.RegisterSet.Esp.ToUInt64():X8}

SegGS: 0x{inst.RegisterSet.SegGs.ToUInt64()}
SegFS: 0x{inst.RegisterSet.SegFs.ToUInt64()}
SegES: 0x{inst.RegisterSet.SegEs.ToUInt64()}
SegDS: 0x{inst.RegisterSet.SegDs.ToUInt64()}
SegCS: 0x{inst.RegisterSet.SegCs.ToUInt64()}
SegSS: 0x{inst.RegisterSet.SegSs.ToUInt64()}";

            }
        }

        private static readonly tracer_wrapper.ApiWrapper TracerApi = new tracer_wrapper.ApiWrapper();
   
        private readonly DispatcherTimer DispatcherTimer = new DispatcherTimer();

        private UiTraceNode lastTraceNode = null;

        private int traceLifetime = 0;
        private UIntPtr currentTraceAddr = UIntPtr.Zero;

        public MainWindow()
        {
            InitializeComponent();

            Console.SetOut(new TextBoxStreamWriter(ConsoleOutputTextbox));

            EventManager.RegisterClassHandler(typeof(TextBox),
                TextBox.KeyUpEvent,
                new System.Windows.Input.KeyEventHandler(TextBox_KeyUp));

            ToolTipService.ShowDurationProperty.OverrideMetadata(
                typeof(DependencyObject), new FrameworkPropertyMetadata(Int32.MaxValue));
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            DispatcherTimer.Tick += new EventHandler(DispatcherTimer_Tick);
            DispatcherTimer.Interval += new TimeSpan(0, 0, 0, 0, 100);
        }

        private FrameworkElement FindVisualChildElement(DependencyObject element, Type childType)
        {
            int count = VisualTreeHelper.GetChildrenCount(element);

            for (int i = 0; i < count; i++)
            {
                var dependencyObject = VisualTreeHelper.GetChild(element, i);
                var fe = (FrameworkElement)dependencyObject;

                if (fe.GetType() == childType)
                {
                    return fe;
                }

                FrameworkElement ret = null;

                if (fe.GetType().Equals(typeof(ScrollViewer)))
                {
                    ret = FindVisualChildElement((fe as ScrollViewer).Content as FrameworkElement, childType);
                }
                else
                {
                    ret = FindVisualChildElement(fe, childType);
                }

                if (ret != null)
                {
                    return ret;
                }
            }

            return null;
        }

        private void TextBox_KeyUp(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Enter) return;

            e.Handled = true;
            StartStopTrace();
        }

        private void AddNode(TracedInstruction traceResult, UiTraceNode parent)
        {
            var newNode = new UiTraceNode(parent, traceResult);
            if (parent != null)
                parent.Items.Add(newNode);
            else
                TraceOutputTreeView.Items.Add(newNode);

            if (traceResult.CallDepth == 0 && traceResult.Type == TracedInstructionType.Return)
                TraceOutputTreeView.Items.Add(new Separator());

            lastTraceNode = newNode;

            var scrollViewer = (ScrollViewer)this.FindVisualChildElement(TraceOutputTreeView, typeof(ScrollViewer));
            scrollViewer.ScrollToBottom();
        }

        private void DispatcherTimer_Tick(object sender, EventArgs e)
        {
            foreach (var traceResult in TracerApi.FetchTraces())
            {
                if (traceResult.CallDepth == 0 && traceResult.Type == TracedInstructionType.Return)
                {
                    if (traceLifetime > 0)
                    {
                        --traceLifetime;

                        if (traceLifetime == 0)
                            StopTrace(traceResult.BranchTarget);
                    }
                }

                if (lastTraceNode == null || traceResult.CallDepth > lastTraceNode.TracedInstruction.CallDepth)
                {
                    AddNode(traceResult, lastTraceNode);
                    continue;
                }

                if (traceResult.CallDepth <= lastTraceNode.TracedInstruction.CallDepth)
                {
                    int numSmaller = lastTraceNode.TracedInstruction.CallDepth - traceResult.CallDepth;

                    var parent = lastTraceNode.ParentNode;

                    for (int i = 0; i < numSmaller; ++i)
                        parent = parent?.ParentNode;

                    AddNode(traceResult, parent);
                    continue;
                }
            }
        }

        private void DetachFromAllProcesses()
        {
            TracerApi.ProcessContext = UIntPtr.Zero;
            TracerApi.DetachProcess(UIntPtr.Zero);

            TraceAddressTextBox.IsEnabled = false;
            TraceDepthTextBox.IsEnabled = false;
            LifetimeTextBox.IsEnabled = false;
            StartStopTraceButton.IsEnabled = false;

            TraceAddressTextBox.TextAlignment = TextAlignment.Center;
            TraceAddressTextBox.Text = "Function";

            TraceDepthTextBox.Text = "Trace Depth";
            TraceDepthTextBox.TextAlignment = TextAlignment.Center;

            LifetimeTextBox.Text = "Lifetime";
            LifetimeTextBox.TextAlignment = TextAlignment.Center;

            StartStopTraceButton.Content = "Start Trace";
        }

        private void ProcessSelectionComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            DetachFromAllProcesses();

            var processId = ((sender as ComboBox)?.SelectedItem as ProcessSelectionItem)?.Process?.Id;

            if (processId != null)
            {
                TracerApi.ProcessContext = TracerApi.AttachProcess(processId.Value);

                if (TracerApi.ProcessContext != UIntPtr.Zero)
                {
                    TraceAddressTextBox.IsEnabled = true;
                    TraceDepthTextBox.IsEnabled = true;
                    LifetimeTextBox.IsEnabled = true;
                    StartStopTraceButton.IsEnabled = true;
                }
            }
        }

        private void ProcessSelectionComboBox_DropDownOpened(object sender, EventArgs e)
        {
            DetachFromAllProcesses();
            ProcessSelectionComboBox.Items.Clear();

            foreach (var process in Process.GetProcesses().OrderBy(p => p.ProcessName))
            {
                try
                {
                    if (!process.Is64BitProcess())
                        (sender as ComboBox)?.Items.Add(new ProcessSelectionItem(process));
                }
                catch (Exception)
                {
                }
            }
        }

        private void StopTrace(UIntPtr stopAddr)
        {
            TracerApi.StopTrace(currentTraceAddr);
            Console.WriteLine($@"Stopped trace at 0x{stopAddr.ToUInt64():X8}.");

            StartStopTraceButton.Content = "Start Trace";

            TraceAddressTextBox.IsEnabled = true;
            TraceDepthTextBox.IsEnabled = true;
            LifetimeTextBox.IsEnabled = true;
            ProcessSelectionComboBox.IsEnabled = true;

            currentTraceAddr = UIntPtr.Zero;
            traceLifetime = 0;
        }

        private void StartStopTrace()
        {
            if (currentTraceAddr != UIntPtr.Zero)
            {
                StopTrace(currentTraceAddr);
                return;
            }

            var address = UIntPtr.Zero;
            try
            {
                address = new UIntPtr(Convert.ToUInt64(TraceAddressTextBox.Text, 16));
            }
            catch (Exception)
            {
                try
                {
                    var splitSymbolName = TraceAddressTextBox.Text.Split('+');
                    if (splitSymbolName.Length == 0 || splitSymbolName.Length > 2)
                        return;

                    var symbolName = splitSymbolName[0];
                    ulong displacement = 0;

                    if (splitSymbolName.Length > 1)
                        displacement = Convert.ToUInt64(splitSymbolName[1], 16);

                    address = new UIntPtr(TracerApi.GetSymbolAddress(symbolName).ToUInt64() + displacement);
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.Message);
                    return;
                }
            }

            if (address == UIntPtr.Zero)
            {
                Console.WriteLine(@"Function address could not be found.");
                return;
            }

            int maxTraceDepth = -1;

            if (TraceDepthTextBox.Text.Length > 0)
            {
                try
                {
                    maxTraceDepth = Convert.ToInt32(TraceDepthTextBox.Text);
                }
                catch (Exception) { }
            }

            int lifetime = 1;

            if (LifetimeTextBox.Text.Length > 0)
            {
                try
                {
                    lifetime = Convert.ToInt32(LifetimeTextBox.Text);
                }
                catch (Exception) { }
            }

            Console.WriteLine($@"Starting trace at 0x{address.ToUInt64():X8}.");

            if (TracerApi.StartTrace(address, -1, maxTraceDepth, lifetime))
            {
                currentTraceAddr = address;
                traceLifetime = lifetime;

                DispatcherTimer.Start();

                TraceAddressTextBox.IsEnabled = false;
                TraceDepthTextBox.IsEnabled = false;
                LifetimeTextBox.IsEnabled = false;
                ProcessSelectionComboBox.IsEnabled = false;

                StartStopTraceButton.Content = "Stop Trace";
            }
        }

        private void StartStopTraceButton_Click(object sender, RoutedEventArgs e)
        {
            StartStopTrace();
        }

        private void AutoClearTextBox_OnGotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (sender is TextBox textbox)
            {
                textbox.Text = string.Empty;
                textbox.TextAlignment = TextAlignment.Left;
            }
        }

        private static bool IsTextAllowed(string text)
        {
            var regex = new Regex(@"[0-9]+");
            return regex.IsMatch(text);
        }

        private void NumericTextBox_OnPreviewTextInput(object sender, TextCompositionEventArgs e)
        {
            e.Handled = !IsTextAllowed(e.Text);
        }

        private void NumericTextBox_OnPasting(object sender, DataObjectPastingEventArgs e)
        {
            if (e.DataObject.GetDataPresent(typeof(string)))
            {
                var text = (string)e.DataObject.GetData(typeof(string));
                if (!IsTextAllowed(text))
                    e.CancelCommand();
            }
            else
            {
                e.CancelCommand();
            }
        }

        private void TraceAddressTextBox_OnLostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (sender is TextBox textbox)
            {
                if (textbox.Text.Length == 0)
                {
                    textbox.Text = "Function";
                    textbox.TextAlignment = TextAlignment.Center;
                }
            }
        }

        private void TraceDepthTextBox_OnLostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (sender is TextBox textbox)
            {
                if (textbox.Text.Length == 0)
                {
                    textbox.Text = "Trace Depth";
                    textbox.TextAlignment = TextAlignment.Center;
                }
            }
        }

        private void LifetimeTextBox_OnLostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (sender is TextBox textbox)
            {
                if (textbox.Text.Length == 0)
                {
                    textbox.Text = "Lifetime";
                    textbox.TextAlignment = TextAlignment.Center;
                }
            }
        }

        private void ClearTraceButton_OnClick(object sender, RoutedEventArgs e)
        {
            TraceOutputTreeView.Items.Clear();
        }
    }
}
