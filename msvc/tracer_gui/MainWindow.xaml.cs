using System;
using System.Diagnostics;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
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
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            DispatcherTimer.Tick += new EventHandler(DispatcherTimer_Tick);
            DispatcherTimer.Interval += new TimeSpan(0, 0, 0, 0, 100);
        }

        private void DispatcherTimer_Tick(object sender, EventArgs e)
        {
            foreach (var traceResult in TracerApi.FetchTraces())
            {
                if (traceLifetime > 0)
                {
                    if (traceResult.CallDepth == 0 && traceResult.Type == TracedInstructionType.Return)
                        --traceLifetime;

                    if (traceLifetime == 0)
                        StopTrace(traceResult.BranchTarget);
                }

                if (lastTraceNode == null)
                {
                    var newNode = new UiTraceNode(null, traceResult);
                    TraceOutputTreeView.Items.Add(newNode);

                    lastTraceNode = newNode;
                    continue;
                }

                if (traceResult.CallDepth > lastTraceNode.TracedInstruction.CallDepth)
                {
                    int numBigger = traceResult.CallDepth - lastTraceNode.TracedInstruction.CallDepth;
                    if (numBigger > 1)
                        Console.WriteLine(@"Trace data seems corrupted!");

                    var newNode = new UiTraceNode(lastTraceNode, traceResult);
                    lastTraceNode.Items.Add(newNode);

                    lastTraceNode = newNode;
                    continue;
                }

                if (traceResult.CallDepth <= lastTraceNode.TracedInstruction.CallDepth)
                {
                    int numSmaller = lastTraceNode.TracedInstruction.CallDepth - traceResult.CallDepth;

                    var parent = lastTraceNode.ParentNode;

                    for (int i = 0; i < numSmaller; ++i)
                        parent = parent?.ParentNode;

                    var newNode = new UiTraceNode(parent, traceResult);
                    if (parent != null)
                        parent.Items.Add(newNode);
                    else
                        TraceOutputTreeView.Items.Add(newNode);

                    lastTraceNode = newNode;
                    continue;
                }
            }
        }

        private void DetachFromAllProcesses()
        {
            TracerApi.ProcessContext = UIntPtr.Zero;
            TracerApi.DetachProcess(UIntPtr.Zero);

            TraceAddressTextBox.IsEnabled = false;
            StartStopTraceButton.IsEnabled = false;

            TraceAddressTextBox.Text = "Function";
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
            Console.WriteLine($@"Stopped trace at 0x{stopAddr.ToUInt64():X8}.");

            StartStopTraceButton.Content = "Start Trace";

            TraceAddressTextBox.IsEnabled = true;
            ProcessSelectionComboBox.IsEnabled = true;

            currentTraceAddr = UIntPtr.Zero;
            traceLifetime = 1;
        }

        private void StartStopTraceButton_Click(object sender, RoutedEventArgs e)
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
                    MessageBox.Show(ex.Message);
                    return;
                }
            }

            if (address == UIntPtr.Zero)
            {
                Console.WriteLine(@"Function address could not be found.");
                return;
            }

            Console.WriteLine($@"Starting trace at 0x{address.ToUInt64():X8}.");

            if (TracerApi.StartTrace(address, -1, -1, 1))
            {
                currentTraceAddr = address;
                traceLifetime = 1;

                DispatcherTimer.Start();

                TraceAddressTextBox.IsEnabled = false;
                ProcessSelectionComboBox.IsEnabled = false;

                StartStopTraceButton.Content = "Stop Trace";
            }
        }

        private void TraceAddressTextBox_OnGotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (sender is TextBox textbox)
                textbox.Text = string.Empty;
        }
    }
}
