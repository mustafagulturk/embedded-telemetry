//
// MPU6050 6-axis IMU model for Renode (loaded at runtime with `include @renode/models/MPU6050.cs`).
//
// I2C address 0x68 (AD0 = 0). Implemented registers:
//   0x19 SMPLRT_DIV, 0x1A CONFIG, 0x1B GYRO_CONFIG (FS_SEL 4:3), 0x1C ACCEL_CONFIG (AFS_SEL 4:3),
//   0x3B..0x48 ACCEL_XOUT_H .. GYRO_ZOUT_L (big-endian, read-only, generated from properties),
//   0x6B PWR_MGMT_1 (reset 0x40 = SLEEP; writing bit7 DEVICE_RESET resets all registers),
//   0x75 WHO_AM_I = 0x68.
// Every other register in 0x00..0x7F is a plain read/write byte (reset 0x00).
//
// Simplifications (documented in renode/README.md):
//   * In SLEEP mode the data registers still return live values.
//   * DLPF / sample rate do not influence the output; each read returns a fresh sample.
//   * STM32F4_I2C calls Read() with count = 1 and keeps whatever we return in its RX FIFO,
//     so Read() returns a whole snapshot from the register pointer up to 0x7F (burst reads are
//     coherent). The pointer is advanced only by `count`.
//
// Fault injection: `Disconnected = true` removes the device from the parent I2C controller's
// address map (the device stays visible in the monitor), so STM32F4_I2C NACKs the address phase
// (SR1.AF = 1, ADDR never set). Setting it back to false re-attaches the device at the same address.
//
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.I2C;

namespace Antmicro.Renode.Peripherals.Sensors
{
    public class MPU6050 : II2CPeripheral
    {
        public MPU6050(IMachine machine)
        {
            this.machine = machine;
            registers = new byte[RegisterCount];
            NoiseSeed = 12345;
            AccelX = 0.0;
            AccelY = 0.0;
            AccelZ = 1.0;
            GyroX = 0.0;
            GyroY = 0.0;
            GyroZ = 0.0;
            Temperature = 25.0;
            Reset();
        }

        public void Reset()
        {
            Array.Clear(registers, 0, registers.Length);
            registers[(int)Registers.PowerManagement1] = 0x40;
            registers[(int)Registers.WhoAmI] = WhoAmIValue;
            registerPointer = 0;
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
            registerPointer = data[0] & 0x7F;
            for(var i = 1; i < data.Length; i++)
            {
                WriteRegister(registerPointer, data[i]);
                registerPointer = (registerPointer + 1) & 0x7F;
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
                // Only reachable if the reflection-based detach failed: an open-drain bus with
                // nobody driving it reads as all ones.
                return Enumerable.Repeat((byte)0xFF, count).ToArray();
            }
            var snapshot = BuildDataSnapshot();
            var length = Math.Max(count, RegisterCount - registerPointer);
            var result = new byte[length];
            for(var i = 0; i < length; i++)
            {
                result[i] = ReadRegister((registerPointer + i) & 0x7F, snapshot);
            }
            this.Log(LogLevel.Noisy, "Read from 0x{0:X2}: {1} byte(s) prepared", registerPointer, length);
            registerPointer = (registerPointer + count) & 0x7F;
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
        public double AccelX { get; set; }       // g
        public double AccelY { get; set; }       // g
        public double AccelZ { get; set; }       // g
        public double GyroX { get; set; }        // deg/s
        public double GyroY { get; set; }        // deg/s
        public double GyroZ { get; set; }        // deg/s
        public double Temperature { get; set; }  // deg C
        public double AccelNoiseStdDev { get; set; }  // g
        public double GyroNoiseStdDev { get; set; }   // deg/s
        public int NoiseSeed { get; set; }

        public int RegisterPointer => registerPointer;
        public bool Sleeping => (registers[(int)Registers.PowerManagement1] & 0x40) != 0;
        public int AccelFullScaleG => 2 << AccelRange;
        public int GyroFullScaleDps => 250 << GyroRange;

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
                this.Log(LogLevel.Info, "MPU6050 {0}", value ? "DISCONNECTED (address phase will be NACKed)" : "reconnected");
            }
        }

        // Monitor helpers: `sysbus.i2c1.mpu6050 ReadRegister 0x75`, `... WriteRegister 0x6B 0x00`
        public byte ReadRegister(int address)
        {
            return ReadRegister(address & 0x7F, BuildDataSnapshot());
        }

        public void WriteRegister(int address, byte value)
        {
            switch((Registers)address)
            {
                case Registers.WhoAmI:
                    return; // read-only
                case Registers.PowerManagement1:
                    if((value & 0x80) != 0)
                    {
                        this.Log(LogLevel.Info, "DEVICE_RESET");
                        var ptr = registerPointer;
                        Reset();
                        registerPointer = ptr;
                        return;
                    }
                    registers[address] = (byte)(value & 0x7F);
                    return;
                default:
                    if(address >= (int)Registers.AccelXH && address <= (int)Registers.GyroZL)
                    {
                        return; // read-only data registers
                    }
                    registers[address] = value;
                    return;
            }
        }

        private byte ReadRegister(int address, byte[] snapshot)
        {
            if(address >= (int)Registers.AccelXH && address <= (int)Registers.GyroZL)
            {
                return snapshot[address - (int)Registers.AccelXH];
            }
            return registers[address];
        }

        private int AccelRange => (registers[(int)Registers.AccelConfig] >> 3) & 0x3;
        private int GyroRange => (registers[(int)Registers.GyroConfig] >> 3) & 0x3;

        private byte[] BuildDataSnapshot()
        {
            var accelLsb = 16384.0 / (1 << AccelRange);            // LSB per g
            var gyroLsb = GyroLsbPerDps[GyroRange];                 // LSB per deg/s
            var values = new short[7];
            values[0] = Saturate((AccelX + Noise(AccelNoiseStdDev)) * accelLsb);
            values[1] = Saturate((AccelY + Noise(AccelNoiseStdDev)) * accelLsb);
            values[2] = Saturate((AccelZ + Noise(AccelNoiseStdDev)) * accelLsb);
            values[3] = Saturate((Temperature - 36.53) * 340.0);
            values[4] = Saturate((GyroX + Noise(GyroNoiseStdDev)) * gyroLsb);
            values[5] = Saturate((GyroY + Noise(GyroNoiseStdDev)) * gyroLsb);
            values[6] = Saturate((GyroZ + Noise(GyroNoiseStdDev)) * gyroLsb);
            var bytes = new byte[14];
            for(var i = 0; i < 7; i++)
            {
                bytes[2 * i] = (byte)((ushort)values[i] >> 8);
                bytes[2 * i + 1] = (byte)((ushort)values[i] & 0xFF);
            }
            return bytes;
        }

        private double Noise(double stdDev)
        {
            if(stdDev <= 0)
            {
                return 0;
            }
            // Box-Muller, deterministic seed -> reproducible runs
            var u1 = 1.0 - random.NextDouble();
            var u2 = random.NextDouble();
            return stdDev * Math.Sqrt(-2.0 * Math.Log(u1)) * Math.Cos(2.0 * Math.PI * u2);
        }

        private static short Saturate(double v)
        {
            v = Math.Round(v);
            if(v > short.MaxValue) return short.MaxValue;
            if(v < short.MinValue) return short.MinValue;
            return (short)v;
        }

        private readonly IMachine machine;
        private readonly byte[] registers;
        private int registerPointer;
        private bool disconnected;
        private int savedAddress = -1;
        private IPeripheral savedParent;
        private Random random;

        private static readonly double[] GyroLsbPerDps = { 131.0, 65.5, 32.8, 16.4 };
        private const int RegisterCount = 0x80;
        private const byte WhoAmIValue = 0x68;

        private enum Registers
        {
            SampleRateDivider = 0x19,
            Config = 0x1A,
            GyroConfig = 0x1B,
            AccelConfig = 0x1C,
            AccelXH = 0x3B,
            GyroZL = 0x48,
            PowerManagement1 = 0x6B,
            WhoAmI = 0x75,
        }

        // Detaches/re-attaches an I2C target from its SimpleContainer parent's address map without
        // unregistering it from the machine (so the monitor name keeps working).
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
