using System.Windows;
using System.Windows.Controls;

namespace tracer_gui
{
    /// <summary>
    /// Interaktionslogik für MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private static readonly tracer_wrapper.ApiWrapper TracerApi = new tracer_wrapper.ApiWrapper();

        public MainWindow()
        {
            InitializeComponent();
        }

        private void ComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            
        }

        private void Button_Click(object sender, RoutedEventArgs e)
        {
            MessageBox.Show(TracerApi.LastErrorMessage);
        }
    }
}
