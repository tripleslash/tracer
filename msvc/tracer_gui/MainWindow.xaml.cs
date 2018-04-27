using System;
using System.Diagnostics;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

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
                Header = $"{TracerApi.FormatInstruction(inst.BranchSource)}";
            }
        }

        private static readonly tracer_wrapper.ApiWrapper TracerApi = new tracer_wrapper.ApiWrapper();
   
        private readonly DispatcherTimer DispatcherTimer = new DispatcherTimer();

        private UiTraceNode lastTraceNode = null;

        public MainWindow()
        {
            InitializeComponent();
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
                        MessageBox.Show("Something weird happened!");

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
                catch (Exception ex)
                {
                }
            }
        }

        private void StartStopTraceButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (TracerApi.StartTrace(new UIntPtr(Convert.ToUInt32(TraceAddressTextBox.Text, 16)), -1, -1, 1))
                {
                    DispatcherTimer.Start();

                    TraceAddressTextBox.IsEnabled = false;
                    ProcessSelectionComboBox.IsEnabled = false;

                    StartStopTraceButton.Content = "Stop Trace";
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
    }
}
