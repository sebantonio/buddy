#!/usr/bin/env python3
"""
Muestra "Hola" en pantalla OLED 128x64 SSD1306 (I2C)
Raspberry Pi Zero 2W

Conexión de pines:
  OLED VCC  --> Pin 1  (3.3V)
  OLED GND  --> Pin 6  (GND)
  OLED SDA  --> Pin 3  (GPIO 2, SDA)
  OLED SCL  --> Pin 5  (GPIO 3, SCL)

Instalación de dependencias:
  sudo apt update
  sudo apt install python3-pip python3-pil -y
  sudo pip3 install adafruit-circuitpython-ssd1306

Habilitar I2C:
  sudo raspi-config -> Interface Options -> I2C -> Enable
"""

import board
import busio
import adafruit_ssd1306
from PIL import Image, ImageDraw, ImageFont

# --- Configuración de pantalla ---
WIDTH  = 128
HEIGHT = 64
I2C_ADDR = 0x3C  # Dirección I2C del SSD1306 (prueba 0x3D si no funciona)

def main():
    # Inicializar bus I2C y pantalla
    i2c = busio.I2C(board.SCL, board.SDA)
    oled = adafruit_ssd1306.SSD1306_I2C(WIDTH, HEIGHT, i2c, addr=I2C_ADDR)

    # Limpiar pantalla
    oled.fill(0)
    oled.show()

    # Crear imagen en blanco
    image = Image.new("1", (WIDTH, HEIGHT))
    draw  = ImageDraw.Draw(image)

    # Intentar cargar fuente grande; usar fuente por defecto si no está disponible
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 40)
    except IOError:
        font = ImageFont.load_default()

    # Calcular posición centrada del texto
    text = "Hola"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    x = (WIDTH  - text_w) // 2
    y = (HEIGHT - text_h) // 2

    # Dibujar texto
    draw.text((x, y), text, font=font, fill=255)

    # Enviar imagen a la pantalla
    oled.image(image)
    oled.show()

    print(f"Mostrando '{text}' en la pantalla OLED. Ctrl+C para salir.")

    # Mantener el programa activo
    try:
        while True:
            pass
    except KeyboardInterrupt:
        oled.fill(0)
        oled.show()
        print("\nPantalla apagada.")

if __name__ == "__main__":
    main()
