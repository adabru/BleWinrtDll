using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace DebugBle
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("You can use this program to test the BleWinrtDll.dll. Make sure your Computer has Bluetooth enabled.");

            BLE ble = new BLE();
            string deviceId = null;

            BLE.BLEScan scan = BLE.ScanDevices();
            scan.Found = (_deviceId, deviceName) =>
            {
                Console.WriteLine("found device with name: " + deviceName);
                if (deviceId == null && deviceName == "CynteractGlove")
                    deviceId = _deviceId;
            };
            scan.Finished = () =>
            {
                Console.WriteLine("scan finished");
                if (deviceId == null)
                    deviceId = "-1";
            };
            while (deviceId == null)
                Thread.Sleep(500);

            scan.Cancel();
            if (deviceId == "-1")
            {
                Console.WriteLine("no device found!");
                return;
            }

            ble.Connect(deviceId,
                "{f6f04ffa-9a61-11e9-a2a3-2a2ae2dbcce4}", 
                new string[] { "{f6f07c3c-9a61-11e9-a2a3-2a2ae2dbcce4}",
                    "{f6f07da4-9a61-11e9-a2a3-2a2ae2dbcce4}",
                    "{f6f07ed0-9a61-11e9-a2a3-2a2ae2dbcce4}" });

            for(int guard = 0; guard < 2000; guard++)
            {
                BLE.ReadPackage();
                BLE.WritePackage(deviceId,
                    "{f6f04ffa-9a61-11e9-a2a3-2a2ae2dbcce4}",
                    "{f6f07ffc-9a61-11e9-a2a3-2a2ae2dbcce4}",
                    new byte[] { 0, 1, 2 });
                Console.WriteLine(BLE.GetError());
                Thread.Sleep(5);
            }

            Console.WriteLine("Press enter to exit the program...");
            Console.ReadLine();
        }
    }
}
