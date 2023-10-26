using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Devices.Usb;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Storage.Streams;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

// 空白ページの項目テンプレートについては、https://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x411 を参照してください

namespace control_uwp
{
    /// <summary>
    /// それ自体で使用できる空白ページまたはフレーム内に移動できる空白ページ。
    /// </summary>
    public sealed partial class MainPage : Page
    {
		public class Model : INotifyPropertyChanged
		{
			private int _SPDIFInVolume;
			public int SPDIFInVolume
			{
				get => _SPDIFInVolume;
				set
				{
					_SPDIFInVolume = value;
					OnPropertyChanged();
				}
			}

			private int _LineInVolume;
			public int LineInVolume
			{
				get => _LineInVolume;
				set
				{
					_LineInVolume = value;
					OnPropertyChanged();
				}
			}

			public event PropertyChangedEventHandler PropertyChanged;

			protected void OnPropertyChanged([CallerMemberName] string name = null)
			{
				PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
			}
		}

		const int VID = 0xcafe;
#if DEVICE_HAS_CDC_OUTPUT
		const int PID = 0x4031;
#else
		const int PID = 0x4030;
#endif
		readonly Guid DeviceGuid = new Guid("8ED17B68-5386-4AB3-8269-B0E237AF481B");

		const int VENDOR_REQUEST_CONTROLLER = 1;
		const int CONTROL_SPDIF_IN_SET_VOLUME = 1;
		const int CONTROL_SPDIF_IN_GET_VOLUME = 2;
		const int CONTROL_LINE_IN_SET_VOLUME = 3;
		const int CONTROL_LINE_IN_GET_VOLUME = 4;

		enum Device
		{
			SPDIFIn, LineIn
		}

		Model model = new Model();
		UsbDevice targetDevice = null;

		public MainPage()
        {
			this.InitializeComponent();
			DataContext = model;

			ApplicationView.PreferredLaunchViewSize = new Size(300, 200);
			ApplicationView.PreferredLaunchWindowingMode = ApplicationViewWindowingMode.PreferredLaunchViewSize;
		}

		private async Task<bool> InitDevice()
		{
			var aqs = UsbDevice.GetDeviceSelector(VID, PID, DeviceGuid);

			var devices = await Windows.Devices.Enumeration.DeviceInformation.FindAllAsync(aqs, null);
			if (devices.Count == 0)
			{
				return false;
			}

			targetDevice = await UsbDevice.FromIdAsync(devices[0].Id);
			return true;
		}

		private async Task<int> GetDeviceVolume(Device device)
		{
			int request = 0;
			switch (device)
			{
				case Device. SPDIFIn:
					request = CONTROL_SPDIF_IN_GET_VOLUME; break;
				case Device.LineIn:
					request = CONTROL_LINE_IN_GET_VOLUME; break;
				default:
					return 0;
			}

			var packet = new UsbSetupPacket()
			{
				RequestType = new UsbControlRequestType
				{
					Direction = UsbTransferDirection.In,
					Recipient = UsbControlRecipient.Device,
					ControlTransferType = UsbControlTransferType.Vendor
				},
				Request = VENDOR_REQUEST_CONTROLLER,
				Index = (byte)request,
				Length = 1
			};

			var buffer = new Windows.Storage.Streams.Buffer(packet.Length);
			var response = await targetDevice.SendControlInTransferAsync(packet, buffer);

			return response.GetByte(0) * 100 / 255;
		}

		private async Task<uint> SetDeivceVolume(Device device, int value)
		{
			int request = 0;
			switch (device)
			{
				case Device.SPDIFIn:
					request = CONTROL_SPDIF_IN_SET_VOLUME; break;
				case Device.LineIn:
					request = CONTROL_LINE_IN_SET_VOLUME; break;
				default:
					return 0;
			}

			var packet = new UsbSetupPacket()
			{
				RequestType = new UsbControlRequestType
				{
					Direction = UsbTransferDirection.Out,
					Recipient = UsbControlRecipient.Device,
					ControlTransferType = UsbControlTransferType.Vendor
				},
				Request = VENDOR_REQUEST_CONTROLLER,
				Index = (byte)request,
				Length = 1
			};

			var writer = new DataWriter();
			writer.WriteByte((byte)value);

			return await targetDevice.SendControlOutTransferAsync(packet, writer.DetachBuffer());
		}

		private async void SPDIFInVolumeSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
		{
			model.SPDIFInVolume = (int)Math.Floor(e.NewValue);
			await SetDeivceVolume(Device.SPDIFIn, model.SPDIFInVolume * 255 / 100);
		}

		private async void LineInVolumeSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
		{
			model.LineInVolume = (int)Math.Floor(e.NewValue);
			await SetDeivceVolume(Device.LineIn, model.LineInVolume * 255 / 100);
		}

		private async void Page_Loaded(object sender, RoutedEventArgs e)
		{
			if (await InitDevice())
			{
				model.SPDIFInVolume = await GetDeviceVolume(Device.SPDIFIn);
				model.LineInVolume = await GetDeviceVolume(Device.LineIn);
			}
			else
			{
				Application.Current.Exit();
			}
		}
	}
}
