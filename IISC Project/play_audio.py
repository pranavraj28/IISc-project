import serial
import serial.tools.list_ports
import pyaudio
import numpy as np
import wave
import os
from datetime import datetime

# ── Config ────────────────────────────────────────────────────────
BAUD_RATE          = 921600
SAMPLE_RATE        = 16000
SAMPLES_PER_PACKET = 320
BYTES_PER_PACKET   = SAMPLES_PER_PACKET * 2

# ── Auto-detect Board B's Serial Port ────────────────────────────
def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        if any(x in desc for x in ["cp210", "ch340", "ftdi", "uart", "esp", "usbserial"]):
            return p.device
    print("\nCould not auto-detect ESP32. Available ports:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device} — {p.description}")
    idx = int(input("Enter port number: "))
    return ports[idx].device

# ── Main ──────────────────────────────────────────────────────────
def main():
    port = find_esp32_port()
    print(f"🔌 Connecting to Board B on {port} at {BAUD_RATE} baud...")

    ser = serial.Serial(port, BAUD_RATE, timeout=2)
    print("✅ Serial connected")

    # Save .wav to Desktop with timestamp
    desktop   = os.path.join(os.path.expanduser("~"), "Desktop")
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    wav_path  = os.path.join(desktop, f"audio_{timestamp}.wav")

    wav_file = wave.open(wav_path, 'wb')
    wav_file.setnchannels(1)
    wav_file.setsampwidth(2)        # int16 = 2 bytes
    wav_file.setframerate(SAMPLE_RATE)
    print(f"💾 Saving to: {wav_path}")

    pa     = pyaudio.PyAudio()
    stream = pa.open(
        format=pyaudio.paInt16,
        channels=1,
        rate=SAMPLE_RATE,
        output=True,
        frames_per_buffer=SAMPLES_PER_PACKET
    )
    print("🔊 Playing audio through Mac speakers...")
    print("   Press Ctrl+C to stop and save the .wav file.\n")

    buffer = b""

    try:
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                continue

            buffer += chunk

            while len(buffer) >= BYTES_PER_PACKET:
                packet = buffer[:BYTES_PER_PACKET]
                buffer = buffer[BYTES_PER_PACKET:]

                samples = np.frombuffer(packet, dtype=np.int16)

                # Volume boost
                boosted = np.clip(samples.astype(np.int32) * 4, -32768, 32767).astype(np.int16)

                # Play through speakers
                stream.write(boosted.tobytes())

                # Save original (unboosted) to wav
                wav_file.writeframes(packet)

    except KeyboardInterrupt:
        print("\n🛑 Stopped.")
    finally:
        stream.stop_stream()
        stream.close()
        pa.terminate()
        ser.close()
        wav_file.close()
        print(f"✅ WAV saved to Desktop: audio_{timestamp}.wav")

if __name__ == "__main__":
    main()
