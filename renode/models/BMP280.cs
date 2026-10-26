//
// BMP280 barometric pressure / temperature sensor model for Renode
// (loaded at runtime with `include @renode/models/BMP280.cs`).
//
// I2C address 0x76 (SDO = GND). Implemented registers:
//   0x88..0x9F calibration (Bosch datasheet example trimming values, little-endian),
//   0xD0 chip id = 0x58, 0xE0 reset (write 0xB6), 0xF3 status (always 0 = ready),
//   0xF4 ctrl_meas (osrs_t 7:5, osrs_p 4:2, mode 1:0), 0xF5 config,
//   0xF7..0xF9 press (20-bit, msb/lsb/xlsb[7:4]), 0xFA..0xFC temp (20-bit).
//
// The raw ADC values are produced by inverting the datasheet integer compensation
// (bmp280_compensate_T_int32 + bmp280_compensate_P_int64) with a binary search, so firmware
// that implements the datasheet formulas gets back Temperature (0.01 degC) and Pressure (Pa)
// within one LSB.
//
// Measurement semantics:
//   * mode 00 (sleep): data registers keep the last conversion (0x80000 after reset).
//   * mode 01/10 (forced): one conversion is latched when ctrl_meas is written, then mode -> 00.
//   * mode 11 (normal): every I2C read transaction returns a fresh conversion.
//   * osrs_x = 0 (skipped): that channel reads 0x80000. Oversetting only enables the channel;
//     resolution is always the full 20 bits. `config` (t_sb, filter) is stored but ignored.
//
// FlightProfile = true generates the altitude from the machine's VIRTUAL time:
//   t < FlightProfileDelay                 : ground (0 m)
//   next 10 s                              : climb to FlightProfileApogee (smooth S-curve)
//   next 10 s                              : hover
//   next 20 s                              : descend to 0 m (smooth S-curve)
//   afterwards                             : ground
// Pressure = GroundPressure * (1 - h/44330)^5.255 (+ Gaussian PressureNoiseStdDev).
//
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.I2C;

namespace Antmicro.Renode.Peripherals.Sensors
{
    public class BMP280 : II2CPeripheral
    {
        public BMP280(IMachine machine)
        {
            this.machine = machine;
            Pressure = 101325.0;
            Temperature = 25.0;
            GroundPressure = 101325.0;
            FlightProfileDelay = 2.0;
            FlightProfileApogee = 120.0;
            NoiseSeed = 4242;
            Reset();
        }

        public void Reset()
        {
            ctrlMeas = 0;
            config = 0;
            registerPointer = 0;
            latchedRawPressure = SkippedValue;
            latchedRawTemperature = SkippedValue;
            random = new Random(NoiseSeed);
        }

        public void Write(byte[] data)
        {
            if(data == null || data.Length == 0)
            {
                return;
            }
            if(disconnected)
            {
                this.Log(LogLevel.Warning, "Write while disconnected ignored");
                return;
            }
            registerPointer = data[0];
            for(var i = 1; i < data.Length; i++)
            {
                WriteRegister(registerPointer, data[i]);
                registerPointer = (registerPointer + 1) & 0xFF;
            }
        }

        public byte[] Read(int count = 1)
        {
            if(count < 1)
            {
                count = 1;
            }
            if(disconnected)
            {
                return Enumerable.Repeat((byte)0xFF, count).ToArray();
            }
            if(Mode == 3)
            {
                Convert(); // normal mode: fresh sample per read transaction (coherent burst)
            }
            // STM32F4_I2C asks for 1 byte and keeps everything we return in its RX FIFO,
            // so return everything up to the end of the register map.
            var length = Math.Max(count, 0x100 - registerPointer);
            var result = new byte[length];
            for(var i = 0; i < length; i++)
            {
                result[i] = ReadRegisterInternal((registerPointer + i) & 0xFF);
            }
            registerPointer = (registerPointer + count) & 0xFF;
            return result;
        }

        // Monitor helpers for the II2CPeripheral interface (the monitor cannot pass byte[]):
        //   sysbus.i2c1.<dev> WriteHex "3B"      -> Write([0x3B])
        //   sysbus.i2c1.<dev> ReadHex 14         -> hex string of Read(14) (first 14 bytes)
        public void WriteHex(string hex)
        {
            hex = hex.Replace(" ", "").Replace("0x", "");
            var bytes = new byte[hex.Length / 2];
            for(var i = 0; i < bytes.Length; i++)
            {
                bytes[i] = System.Convert.ToByte(hex.Substring(2 * i, 2), 16);
            }
            Write(bytes);
        }

        public string ReadHex(int count)
        {
            return string.Join(" ", Read(count).Take(count).Select(b => b.ToString("X2")));
        }

        public void FinishTransmission()
        {
        }

        // ----- monitor-settable properties -----
        public double Pressure { get; set; }              // Pa (used when FlightProfile = false)
        public double Temperature { get; set; }           // deg C
        public double PressureNoiseStdDev { get; set; }   // Pa
        public int NoiseSeed { get; set; }
        public bool FlightProfile { get; set; }
        public double FlightProfileDelay { get; set; }    // s of virtual time on the ground first
        public double FlightProfileApogee { get; set; }   // m
        public double GroundPressure { get; set; }        // Pa, p0 for the barometric formula

        public double VirtualTimeSeconds => machine.ElapsedVirtualTime.TimeElapsed.TotalSeconds;
        public double ProfileAltitude => FlightProfileAltitude(VirtualTimeSeconds);
        public double CurrentTargetPressure => FlightProfile ? AltitudeToPressure(ProfileAltitude) : Pressure;
        public int LastRawPressure => latchedRawPressure;
        public int LastRawTemperature => latchedRawTemperature;
        public int Mode => ctrlMeas & 0x3;

        public bool Disconnected
        {
            get => disconnected;
            set
            {
                if(value == disconnected)
                {
                    return;
                }
                disconnected = value;
                I2CDetachHelper.SetAttached(machine, this, !value, ref savedAddress, ref savedParent);
                this.Log(LogLevel.Info, "BMP280 {0}", value ? "DISCONNECTED (address phase will be NACKed)" : "reconnected");
            }
        }

        // Monitor helpers
        public byte ReadRegister(int address)
        {
            return ReadRegisterInternal(address & 0xFF);
        }

        public void WriteRegister(int address, byte value)
        {
            switch(address)
            {
                case RegReset:
                    if(value == 0xB6)
                    {
                        this.Log(LogLevel.Info, "Soft reset");
                        var ptr = registerPointer;
                        Reset();
                        registerPointer = ptr;
                    }
                    break;
                case RegCtrlMeas:
                    ctrlMeas = value;
                    if(Mode == 1 || Mode == 2)
                    {
                        Convert();
                        ctrlMeas &= 0xFC; // forced conversion done -> back to sleep
                    }
                    else if(Mode == 3)
                    {
                        Convert();
                    }
                    break;
                case RegConfig:
                    config = value;
                    break;
                default:
                    this.Log(LogLevel.Debug, "Write to read-only/unimplemented register 0x{0:X2} ignored", address);
                    break;
            }
        }

        // Forces a conversion now (useful from the monitor).
        public void Convert()
        {
            var osrsT = (ctrlMeas >> 5) & 0x7;
            var osrsP = (ctrlMeas >> 2) & 0x7;
            var targetT = Temperature;
            var targetP = CurrentTargetPressure + Gaussian(PressureNoiseStdDev);

            var adcT = InvertTemperature(targetT);
            var tFine = CompensateTemperatureFine(adcT);
            latchedRawTemperature = osrsT == 0 ? SkippedValue : adcT;
            latchedRawPressure = osrsP == 0 ? SkippedValue : InvertPressure(targetP, tFine);
        }

        // ---- Bosch datasheet integer compensation (public so it can be checked from the monitor) ----
        public int CompensateTemperature(int adcT)
        {
            return (CompensateTemperatureFine(adcT) * 5 + 128) >> 8; // 0.01 degC
        }

        public uint CompensatePressure(int adcP, int adcT)
        {
            return CompensatePressureQ24_8(adcP, CompensateTemperatureFine(adcT)) / 256; // Pa
        }

        private static int CompensateTemperatureFine(int adcT)
        {
            int var1 = (((adcT >> 3) - ((int)DigT1 << 1)) * DigT2) >> 11;
            int var2 = (((((adcT >> 4) - (int)DigT1) * ((adcT >> 4) - (int)DigT1)) >> 12) * DigT3) >> 14;
            return var1 + var2;
        }

        private static uint CompensatePressureQ24_8(int adcP, int tFine)
        {
            long var1 = (long)tFine - 128000;
            long var2 = var1 * var1 * DigP6;
            var2 = var2 + ((var1 * DigP5) << 17);
            var2 = var2 + ((long)DigP4 << 35);
            var1 = ((var1 * var1 * DigP3) >> 8) + ((var1 * DigP2) << 12);
            var1 = (((1L << 47) + var1) * DigP1) >> 33;
            if(var1 == 0)
            {
                return 0;
            }
            long p = 1048576 - adcP;
            p = (((p << 31) - var2) * 3125) / var1;
            var1 = ((long)DigP9 * (p >> 13) * (p >> 13)) >> 25;
            var2 = ((long)DigP8 * p) >> 19;
            p = ((p + var1 + var2) >> 8) + ((long)DigP7 << 4);
            return (uint)p;
        }

        private static int InvertTemperature(double celsius)
        {
            // compensated T is increasing in adc_T: find adc_T whose output is closest to target
            var target = (int)Math.Round(celsius * 100.0);
            int lo = 0, hi = MaxRaw;
            while(lo < hi)
            {
                var mid = lo + (hi - lo) / 2;
                if(((CompensateTemperatureFine(mid) * 5 + 128) >> 8) < target) lo = mid + 1; else hi = mid;
            }
            return lo;
        }

        private static int InvertPressure(double pascal, int tFine)
        {
            // compensated P (Q24.8) is decreasing in adc_P
            var target = (long)Math.Round(pascal * 256.0);
            int lo = 0, hi = MaxRaw;
            while(lo < hi)
            {
                var mid = lo + (hi - lo) / 2;
                if((long)CompensatePressureQ24_8(mid, tFine) > target) lo = mid + 1; else hi = mid;
            }
            // lo = first adc with p <= target; pick the closer of lo / lo-1
            if(lo > 0 && Math.Abs((long)CompensatePressureQ24_8(lo - 1, tFine) - target) < Math.Abs((long)CompensatePressureQ24_8(lo, tFine) - target))
            {
                lo--;
            }
            return lo;
        }

        private double FlightProfileAltitude(double t)
        {
            t -= FlightProfileDelay;
            if(t <= 0) return 0;
            if(t < 10) return FlightProfileApogee * SmoothStep(t / 10.0);
            t -= 10;
            if(t < 10) return FlightProfileApogee;
            t -= 10;
            if(t < 20) return FlightProfileApogee * (1.0 - SmoothStep(t / 20.0));
            return 0;
        }

        private double AltitudeToPressure(double h)
        {
            return GroundPressure * Math.Pow(1.0 - h / 44330.0, 5.255);
        }

        private static double SmoothStep(double x)
        {
            return x * x * (3 - 2 * x);
        }

        private double Gaussian(double stdDev)
        {
            if(stdDev <= 0)
            {
                return 0;
            }
            var u1 = 1.0 - random.NextDouble();
            var u2 = random.NextDouble();
            return stdDev * Math.Sqrt(-2.0 * Math.Log(u1)) * Math.Cos(2.0 * Math.PI * u2);
        }

        private byte ReadRegisterInternal(int address)
        {
            if(address >= CalibStart && address < CalibStart + Calibration.Length)
            {
                return Calibration[address - CalibStart];
            }
            switch(address)
            {
                case RegChipId: return ChipId;
                case RegReset: return 0;
                case RegStatus: return 0;
                case RegCtrlMeas: return ctrlMeas;
                case RegConfig: return config;
                case 0xF7: return (byte)(latchedRawPressure >> 12);
                case 0xF8: return (byte)(latchedRawPressure >> 4);
                case 0xF9: return (byte)((latchedRawPressure & 0xF) << 4);
                case 0xFA: return (byte)(latchedRawTemperature >> 12);
                case 0xFB: return (byte)(latchedRawTemperature >> 4);
                case 0xFC: return (byte)((latchedRawTemperature & 0xF) << 4);
                default: return 0;
            }
        }

        private static byte[] BuildCalibration()
        {
            var words = new int[] { DigT1, DigT2, DigT3, DigP1, DigP2, DigP3, DigP4, DigP5, DigP6, DigP7, DigP8, DigP9 };
            var bytes = new byte[24];
            for(var i = 0; i < words.Length; i++)
            {
                bytes[2 * i] = (byte)(words[i] & 0xFF);
                bytes[2 * i + 1] = (byte)((words[i] >> 8) & 0xFF);
            }
            return bytes;
        }

        private readonly IMachine machine;
        private byte ctrlMeas;
        private byte config;
        private int registerPointer;
        private int latchedRawPressure;
        private int latchedRawTemperature;
        private bool disconnected;
        private int savedAddress = -1;
        private IPeripheral savedParent;
        private Random random;

        private const ushort DigT1 = 27504;
        private const short DigT2 = 26435;
        private const short DigT3 = -1000;
        private const ushort DigP1 = 36477;
        private const short DigP2 = -10685;
        private const short DigP3 = 3024;
        private const short DigP4 = 2855;
        private const short DigP5 = 140;
        private const short DigP6 = -7;
        private const short DigP7 = 15500;
        private const short DigP8 = -14600;
        private const short DigP9 = 6000;
        private static readonly byte[] Calibration = BuildCalibration();

        private const int CalibStart = 0x88;
        private const int RegChipId = 0xD0;
        private const int RegReset = 0xE0;
        private const int RegStatus = 0xF3;
        private const int RegCtrlMeas = 0xF4;
        private const int RegConfig = 0xF5;
        private const byte ChipId = 0x58;
        private const int SkippedValue = 0x80000;
        private const int MaxRaw = 0xFFFFF;

        // Same helper as in MPU6050.cs (each runtime-included file is a separate assembly).
        private static class I2CDetachHelper
        {
            public static void SetAttached(IMachine machine, II2CPeripheral self, bool attach, ref int savedAddress, ref IPeripheral savedParent)
            {
                try
                {
                    if(!attach)
                    {
                        savedParent = machine.GetParentPeripherals(self).FirstOrDefault();
                        var map = GetMap(savedParent);
                        if(map == null)
                        {
                            self.Log(LogLevel.Warning, "Cannot detach from parent; reads will return 0xFF instead of NACK");
                            return;
                        }
                        foreach(var kv in map.Where(x => ReferenceEquals(x.Value, self)).ToList())
                        {
                            savedAddress = kv.Key;
                            map.Remove(kv.Key);
                        }
                    }
                    else
                    {
                        var map = GetMap(savedParent);
                        if(map != null && savedAddress >= 0 && !map.ContainsKey(savedAddress))
                        {
                            map.Add(savedAddress, self);
                        }
                    }
                }
                catch(Exception e)
                {
                    self.Log(LogLevel.Warning, "I2C detach helper failed: {0}", e.Message);
                }
            }

            private static Dictionary<int, II2CPeripheral> GetMap(IPeripheral parent)
            {
                if(parent == null)
                {
                    return null;
                }
                for(var t = parent.GetType(); t != null; t = t.BaseType)
                {
                    var field = t.GetField("ChildCollection", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly);
                    if(field != null)
                    {
                        return field.GetValue(parent) as Dictionary<int, II2CPeripheral>;
                    }
                }
                return null;
            }
        }
    }
}
