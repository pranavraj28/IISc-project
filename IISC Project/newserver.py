
import asyncio
import os
import websockets
import http

connected_clients = set()

# FIX 1: ONE argument only. This perfectly matches your updated library.
async def audio_broker(websocket):
    connected_clients.add(websocket)
    print(f"🟢 Device connected! Total devices: {len(connected_clients)}", flush=True)

    try:
        async for message in websocket:
            if connected_clients:
                others = [c for c in connected_clients if c != websocket]
                if others:
                    await asyncio.gather(
                        *[client.send(message) for client in others],
                        return_exceptions=True 
                    )
    except websockets.exceptions.ConnectionClosedOK:
        print("🔴 Device disconnected cleanly.", flush=True)
    except Exception as e:
        print(f"💥 Unexpected error: {e}", flush=True)
    finally:
        connected_clients.discard(websocket)
        print(f"👥 Remaining devices: {len(connected_clients)}", flush=True)

# FIX 2: This safely intercepts Render's health checks without crashing
def health_check(connection, request):
    # If Render is just pinging to see if the server is alive, say "OK"
    if getattr(request, "method", "") == "HEAD":
        return http.HTTPStatus.OK, [], b"OK\n"
    # Otherwise, let the ESP32 connect normally
    return None 

async def main():
    port = int(os.environ.get("PORT", 8765))
    print(f"🚀 Cloud Audio Server starting on port {port}", flush=True)

    async with websockets.serve(
        audio_broker,
        "0.0.0.0",
        port,
        ping_interval=20,
        ping_timeout=10,
        max_size=2 * 1024 * 1024,
        process_request=health_check # Attaches the Render bypass
    ):
        print(f"✅ Server listening on port {port}", flush=True)
        await asyncio.Future() 

if __name__ == "__main__":
    asyncio.run(main())
