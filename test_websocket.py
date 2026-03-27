import websocket
import time

def on_message(ws, message):
    print(f"收到消息: {message}")

def on_error(ws, error):
    print(f"错误: {error}")

def on_close(ws, close_status_code, close_msg):
    print("连接已关闭")

def on_open(ws):
    print("连接已建立")
    # 发送测试消息
    ws.send("Hello ESP32!")
    # 等待响应
    time.sleep(1)
    # 测试异步消息
    ws.send("Trigger async")

if __name__ == "__main__":
    # 连接到WebSocket服务器
    websocket.enableTrace(True)  # 启用调试信息
    ws = websocket.WebSocketApp("ws://192.168.4.1/ws",
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)

    ws.run_forever()