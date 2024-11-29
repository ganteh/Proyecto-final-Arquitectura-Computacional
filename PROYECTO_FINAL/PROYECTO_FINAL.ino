/**
 * @file SistemaSeguridadAvanzado.ino
 * @brief Sistema de seguridad avanzado con monitoreo ambiental, seguimiento de eventos y funcionalidad de alarma
 * 
 * Este proyecto de Arduino implementa un sistema de seguridad multiestado con las siguientes características:
 * - Acceso protegido por contraseña
 * - Monitoreo ambiental (temperatura, humedad, luz)
 * - Detección de movimiento y campo magnético
 * - Pantalla LCD para estado e información
 * - LED RGB y zumbador para alertas
 * 
 * @author [Cristobal Villaquiran, Jhon Steven Zuniga, Yezid Esteban Hernandez]
 * @date [28/11/2024]
 * @version 1.0
 */

#include "StateMachineLib.h"
#include "AsyncTaskLib.h"
#include "DHT.h"
#include <LiquidCrystal.h>
#include <Keypad.h>

// Configuración del LCD
/**
 * @brief Configuración de pines para la pantalla LCD
 * 
 * Define las conexiones de pines para la pantalla de cristal líquido
 */
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// Configuración del teclado
/**
 * @brief Distribución de teclado y configuración de pines
 * 
 * Define el diseño de la matriz de teclado y las conexiones de pines
 */
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 24, 26, 28, 30 };
byte colPins[COLS] = { 32, 34, 36, 38 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Configuración de sensores y pines
/**
 * @brief Configuración del sensor de temperatura y humedad DHT22
 */
#define DHTPIN 46
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
#define TEMPERATURA_UMBRAL 30.0

/**
 * @brief Definiciones de pines para diversos sensores y componentes
 */
#define PIN_SENSOR_HALL A1
#define BUZZER_PIN 43
#define LDR_PIN A0
#define PIN_PIR 48
#define LED_PIN 13

/**
 * @brief Configuración de pines para LED RGB
 */
#define RGB_RED_PIN 6
#define RGB_GREEN_PIN 7
#define RGB_BLUE_PIN 8
#define CATHODE_COMMON true  // Cambiar a `false` si el LED es de ánodo común

// Definiciones de la Máquina de Estados
/**
 * @brief Enumeración de los posibles estados del sistema
 */
enum State {
  INICIO,               ///< Estado inicial, esperando contraseña
  BLOQUEADO,            ///< Estado de bloqueo después de múltiples intentos incorrectos
  MONITOREO_AMBIENTAL,  ///< Estado de monitoreo ambiental
  MONITOR_EVENTOS,      ///< Estado de monitoreo de eventos
  ALARMA                ///< Estado de alarma activada
};

/**
 * @brief Enumeración de entradas/eventos posibles del sistema
 */
enum Input {
  INPUT_T,       ///< Evento de temporizador/tiempo agotado
  INPUT_P,       ///< Umbral de temperatura excedido
  INPUT_S,       ///< Evento de sensor (PIR o sensor Hall)
  INPUT_UNKNOWN  ///< Predeterminado, sin acción específica
};

/// Objeto de máquina de estados con 5 estados y 8 transiciones
StateMachine stateMachine(5, 8);
/// Entrada actual del sistema
Input input = INPUT_UNKNOWN;

// Variables Globales
/// Contraseña que se está ingresando actualmente
String contrasenaIngresada = "";
/// Contraseña correcta del sistema
String contrasenaCorrecta = "4561";
/// Lecturas actuales de temperatura y humedad
float temperatura = 0.0, humedad = 0.0;
/// Lecturas actuales de luz y sensor PIR
int luz = 0, pirEstado = 0;
/// Variable para contar intentos fallidos
int intentosFallidos = 0; 
/// Número máximo de intentos
const int MAX_INTENTOS = 3; 

/**
 * @brief Bandera para indicar si debe ocurrir una transición de estado
 */
bool transicion_desencadenada = false;

/// Último momento en que se cambió el estado del LED
unsigned long ultimo_Tiempo_Led = 0;
/// Último momento en que se cambió el estado del zumbador
unsigned long ultimo_tiempo_buzzer = 0;
/// Intervalo para parpadeo del LED
unsigned long ledIntervalo = 500;
/// Estado actual del LED
bool ledEstado = false;
/// Estado actual del zumbador
bool buzzerEstado = false;

// Tareas Asíncronas
/**
 * @brief Tarea asíncrona para monitorear la temperatura y activar la alarma si se supera el umbral
 */
AsyncTask TaskTemperatura(500, true, []() {
  temperatura = dht.readTemperature();
  humedad = dht.readHumidity();
  if (temperatura > TEMPERATURA_UMBRAL && stateMachine.GetState() == MONITOREO_AMBIENTAL) {
    input = INPUT_P;
    Serial.println("Temperatura fuera del umbral. Activando alarma");
  }
});

AsyncTask TaskLuz(500, true, []() {
  luz = analogRead(LDR_PIN);
});

AsyncTask TaskInfraRojo(500, true, []() {
  pirEstado = digitalRead(PIN_PIR);
  if (pirEstado == HIGH && stateMachine.GetState() == MONITOR_EVENTOS) {
    input = INPUT_S;
    Serial.println("Movimiento detectado. Activando alarma");
  }
});

AsyncTask TaskMonitoreoAmbiental(5000, false, []() {
  if (stateMachine.GetState() == MONITOREO_AMBIENTAL) {
    transicion_desencadenada = true;
  }
});

AsyncTask TaskMonitorEventos(3000, false, []() {
  if (stateMachine.GetState() == MONITOR_EVENTOS) {
    transicion_desencadenada = true;
  }
});

AsyncTask TaskBloqueoTiempo(7000, false, []() {
  if (stateMachine.GetState() == BLOQUEADO) {
    input = INPUT_T;  // Transición al estado INICIO
    Serial.println("Tiempo de bloqueo terminado. Regresando al estado INICIO.");
  }
});

// Funciones de Tareas y Estados
/**
 * @brief Establece el color del LED RGB
 * 
 * @param red Intensidad del color rojo (0-255)
 * @param green Intensidad del color verde (0-255)
 * @param blue Intensidad del color azul (0-255)
 */
void setRGBColor(int red, int green, int blue) {
  if (CATHODE_COMMON) {
    analogWrite(RGB_RED_PIN, red);
    analogWrite(RGB_GREEN_PIN, green);
    analogWrite(RGB_BLUE_PIN, blue);
  } else {
    analogWrite(RGB_RED_PIN, 255 - red);
    analogWrite(RGB_GREEN_PIN, 255 - green);
    analogWrite(RGB_BLUE_PIN, 255 - blue);
  }
}

/**
 * @brief Apaga completamente el LED RGB
 */
void apagarRGB(void) {
  setRGBColor(0, 0, 0);  // Todos los canales apagados
}

/**
 * @brief Controla el parpadeo del LED en función de un intervalo de tiempo
 * 
 * @param interval Intervalo de tiempo entre cambios de estado del LED
 */
void MantenerLed(unsigned long interval) {
  if (millis() - ultimo_Tiempo_Led >= interval) {
    ultimo_Tiempo_Led = millis();
    ledEstado = !ledEstado;
    digitalWrite(LED_PIN, ledEstado);
  }
}

/**
 * @brief Controla el zumbador para crear un sonido de alarma
 * 
 * Alterna entre dos tonos diferentes cada 150 milisegundos
 */
void MantenerBuzzer(void) {
  // Alternar tono cada 150 ms
  if (millis() - ultimo_tiempo_buzzer >= 150) {
    ultimo_tiempo_buzzer = millis();
    buzzerEstado = !buzzerEstado;
    if (buzzerEstado) {
      tone(BUZZER_PIN, 1000);  // Tono 1000 Hz
    } else {
      tone(BUZZER_PIN, 1500);  // Tono 1500 Hz
    }
  }
}

/**
 * @brief Configuración inicial al entrar en el estado BLOQUEADO
 * 
 * Configura la pantalla LCD, inicia el temporizador de bloqueo y activa señales visuales y sonoras
 */
void Bloqueado(void) {
  lcd.clear();
  lcd.print("BLOQUEADO 7s");
  Serial.println("Entrando al estado BLOQUEADO");
  ledIntervalo = 500;     // Parpadeo cada 500 ms
  tone(BUZZER_PIN, 500);  // Zumbador a 500 Hz constante
  TaskBloqueoTiempo.Start();
}

/**
 * @brief Realiza las acciones necesarias al salir del estado BLOQUEADO.
 *
 * Esta función detiene el sonido del buzzer, apaga el LED, reinicia el contador 
 * de intentos fallidos y detiene el temporizador asociado al estado BLOQUEADO.
 * Se ejecuta automáticamente cuando la máquina de estados transita 
 * fuera del estado BLOQUEADO.
 */
void salir_Bloqueado(void){
  Serial.println("Saliendo del estado BLOQUEADO");
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
  intentosFallidos = 0; // Reiniciar los intentos fallidos al salir del estado BLOQUEADO
  TaskBloqueoTiempo.Stop();// Detiene el temporizador
}

/**
 * @brief Configuración inicial al entrar en el estado INICIO.
 *
 * Esta función realiza las acciones necesarias para preparar el sistema
 * al entrar en el estado INICIO. Esto incluye:
 * - Restablecer el estado del sensor PIR.
 * - Reiniciar el estado de entrada a desconocido.
 * - Limpiar la contraseña ingresada.
 * - Limpiar y actualizar la pantalla LCD para solicitar una clave.
 * - Registrar un mensaje en el puerto serie indicando el ingreso al estado.
 */
void Inicio(void) {
  pirEstado = 0;
  input = INPUT_UNKNOWN;
  contrasenaIngresada = "";
  lcd.clear();
  lcd.print("Ingrese clave:");
  Serial.println("Entrando al estado INICIO");
}
/**
 * @brief Acciones al salir del estado INICIO.
 *
 * Esta función limpia la pantalla LCD y registra en el monitor serial 
 * que se ha salido del estado INICIO.
 *
 * @note Esta función es llamada automáticamente al salir del estado INICIO.
 */
void salir_Inicio(void) {
  Serial.println("Saliendo del estado INICIO");
  lcd.clear();
}

/**
 * @brief Acciones al entrar en el estado MONITOREO_AMBIENTAL.
 *
 * Esta función inicializa las lecturas de los sensores y actualiza la pantalla LCD 
 * con la información ambiental (temperatura, humedad y luz).
 * 
 * - Limpia el LCD y muestra los valores iniciales de los sensores en la pantalla.
 * - Lee manualmente la temperatura, humedad y luz para mostrar datos actualizados al ingresar al estado.
 * - Inicia las tareas asincrónicas para realizar lecturas periódicas de temperatura, luz y el temporizador del estado.
 * - Reinicia la variable de transición desencadenada.
 *
 * @note Esta función es llamada automáticamente al entrar en el estado MONITOREO_AMBIENTAL.
 */
void Monitoreo(void) {
  lcd.clear();
  // Leer sensores manualmente en el estado MONITOREO_AMBIENTAL
  temperatura = dht.readTemperature();
  humedad = dht.readHumidity();
  luz = analogRead(LDR_PIN);
  lcd.print("T: ");
  lcd.print(temperatura, 1);
  lcd.print("C H:");
  lcd.print(humedad, 1);
  lcd.setCursor(0, 1);
  lcd.print("Luz: ");
  lcd.print(luz);
  Serial.println("Entrando al estado MONITOREO_AMBIENTAL");
  TaskTemperatura.Start();
  TaskLuz.Start();
  TaskMonitoreoAmbiental.Start();
  transicion_desencadenada = false;
}

/**
 * @brief Acciones al salir del estado MONITOREO_AMBIENTAL.
 *
 * Esta función detiene las tareas relacionadas con el monitoreo ambiental y limpia la pantalla LCD.
 * 
 * - Detiene las tareas asincrónicas asociadas a la lectura de temperatura, luz, y temporizador del monitoreo ambiental.
 * - Limpia el contenido del LCD.
 *
 * @note Esta función es llamada automáticamente al salir del estado MONITOREO_AMBIENTAL.
 */
void salir_Monitoreo(void) {
  Serial.println("Saliendo del estado MONITOREO_AMBIENTAL");
  TaskTemperatura.Stop();
  TaskLuz.Stop();
  TaskMonitoreoAmbiental.Stop();
  lcd.clear();
}

/**
 * @brief Configuración inicial al entrar en el estado MONITOR_EVENTOS.
 *
 * Esta función realiza las acciones necesarias para iniciar el monitoreo de eventos relacionados
 * con los sensores PIR e Hall (simulado como un botón).
 *
 * - Limpia la pantalla LCD y actualiza la información de los sensores PIR y Hall.
 * - Muestra en el LCD si el sensor PIR está activo o inactivo.
 * - Muestra el estado del sensor Hall (simulado como ALTO o BAJO).
 * - Si el sensor Hall está en estado HIGH, activa la alarma cambiando el input a `INPUT_S`.
 * - Inicia las tareas asincrónicas para el monitoreo de sensores infrarrojos y otros eventos.
 * - Establece la variable `transicion_desencadenada` como `false`.
 *
 * @note Esta función es llamada automáticamente al ingresar al estado MONITOR_EVENTOS.
 */
void Eventos(void) {
  lcd.clear();
  // Leer estado del botón (simulando el sensor Hall)
  int estadoBotonHall = digitalRead(PIN_SENSOR_HALL);
  // Leer estado del sensor PIR
  pirEstado = digitalRead(PIN_PIR);
  // Mostrar información en el LCD
  lcd.print("PIR: ");
  lcd.print(pirEstado ? "Activo" : "Inactivo");
  lcd.setCursor(0, 1);
  lcd.print("HALL: ");
  lcd.print(estadoBotonHall == HIGH ? "ALTO" : "BAJO");
  // Activar alarma si el botón está en HIGH mientras se implementa sensor hall
  if (estadoBotonHall == HIGH) {
    input = INPUT_S;
    Serial.println("Campo magnético alto detectado. Activando alarma");
  }
  Serial.print("Estado del botón Hall: ");
  Serial.println(estadoBotonHall == HIGH ? "ALTO" : "BAJO");
  Serial.println("Entrando al estado MONITOR_EVENTOS");
  TaskInfraRojo.Start();
  TaskMonitorEventos.Start();
  transicion_desencadenada = false;
}

/**
 * @brief Realiza las acciones necesarias al salir del estado MONITOR_EVENTOS.
 *
 * Esta función detiene las tareas de monitoreo relacionadas con sensores
 * (infrarrojo y eventos) y limpia la pantalla LCD. 
 * Se ejecuta automáticamente cuando la máquina de estados transita 
 * fuera del estado MONITOR_EVENTOS.
 */
void salir_Eventos(void) {
  Serial.println("Saliendo del estado MONITOR_EVENTOS");
  TaskInfraRojo.Stop();
  TaskMonitorEventos.Stop();
  lcd.clear();
}

/**
 * @brief Configuración inicial al entrar en el estado ALARMA.
 *
 * Esta función realiza las acciones necesarias al activar la alarma del sistema.
 * - Limpia la pantalla LCD y muestra un mensaje basado en la causa de la alarma:
 *   - "TEMP ALTA" si la temperatura supera el umbral configurado.
 *   - "MOV DETECTADO" si se detectó movimiento.
 * - Espera brevemente (1.5 segundos) para permitir la lectura del mensaje inicial.
 * - Limpia nuevamente la pantalla LCD y muestra "ALARMA ACTIVADA".
 * - Configura el intervalo de parpadeo del LED a 150 ms.
 * - Registra en el puerto serie que se ha ingresado al estado ALARMA.
 */
void Alarma(void) {
  lcd.clear();
  if (temperatura > TEMPERATURA_UMBRAL) {
    lcd.print("TEMP ALTA");
  } else {
    lcd.print("MOV DETECTADO");
  }
  delay(1500);
  lcd.clear();
  lcd.print("ALARMA ACTIVADA");
  Serial.println("Entrando al estado ALARMA");
  ledIntervalo = 150;  // Parpadeo cada 150 ms
}

/**
 * @brief Realiza las acciones necesarias al salir del estado ALARMA.
 *
 * Esta función apaga el buzzer, apaga el LED de estado y limpia la pantalla LCD.
 * Se ejecuta automáticamente cuando la máquina de estados transita 
 * fuera del estado ALARMA.
 */
void salir_Alarma(void) {
  Serial.println("Saliendo del estado ALARMA");
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
  lcd.clear();
}

/**
 * @brief Función para leer la entrada del teclado
 * 
 * Maneja:
 * - Ingreso de contraseña en el estado INICIO
 * - Desactivación de alarma con tecla '#'
 * - Validación de contraseña
 * - Retroalimentación visual mediante LCD y LED RGB
 */
void leer_teclado(void) {
  char tecla = keypad.getKey();
  if (tecla) {
    if (stateMachine.GetState() == ALARMA && tecla == '#') {
      input = INPUT_T;
      Serial.println("Tecla # presionada. Desactivando alarma");
      return;
    }
    if (stateMachine.GetState() == INICIO) {
      if (tecla == '#') {
        if (contrasenaIngresada == contrasenaCorrecta) {
          input = INPUT_T;
          intentosFallidos = 0; // Resetear los intentos fallidos si la contraseña es correcta
          setRGBColor(0, 255, 0); // Solo verde
          delay(1000);           // Mostrar el color durante 1 segundo
          apagarRGB();           // Apagar después
        } else {
          contrasenaIngresada = "";
          intentosFallidos++; // Incrementar el contador de intentos fallidos
          lcd.clear();
          lcd.print("Ingrese otra vez");
          
          // Si el número de intentos fallidos es mayor o igual al máximo, bloquear el sistema
          if (intentosFallidos >= MAX_INTENTOS) {
            input = INPUT_S;  // Activa la transición a BLOQUEADO
            Serial.println("Demasiados intentos fallidos. Entrando en estado BLOQUEADO.");
          }

          delay(1000);
          setRGBColor(0, 0, 255); // Azul: contraseña incorrecta
          delay(1000); // Mostrar el color durante 1 segundo
          apagarRGB(); // Apagar el LED después
          return;
        }
      } else if (tecla == '*') {
        contrasenaIngresada = "";
      } else if (contrasenaIngresada.length() < 4) {
        contrasenaIngresada += tecla;
      }

      // Mostrar los asteriscos en lugar de la contraseña real
      lcd.setCursor(0, 1);
      for (int i = 0; i < contrasenaIngresada.length(); i++) {
        lcd.print('*');
      }
      // Rellenar con espacios si la longitud es menor a 4
      for (int i = contrasenaIngresada.length(); i < 4; i++) {
        lcd.print(' ');
      }
    }
  }
}


/**
 * @brief Función de configuración inicial del sistema
 * 
 * Inicializa:
 * - Comunicación serie
 * - Pantalla LCD
 * - Sensores
 * - Pines de entrada/salida
 * - Máquina de estados con transiciones y devoluciones de llamada
 */
void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  dht.begin();
  pinMode(PIN_PIR, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  apagarRGB();

  stateMachine.AddTransition(INICIO, MONITOREO_AMBIENTAL, []() {
    return input == INPUT_T;
  });
  stateMachine.AddTransition(MONITOREO_AMBIENTAL, MONITOR_EVENTOS, []() {
    return transicion_desencadenada;
  });
  stateMachine.AddTransition(MONITOR_EVENTOS, MONITOREO_AMBIENTAL, []() {
    return transicion_desencadenada;
  });
  stateMachine.AddTransition(MONITOREO_AMBIENTAL, ALARMA, []() {
    return input == INPUT_P;
  });
  stateMachine.AddTransition(MONITOR_EVENTOS, ALARMA, []() {
    return input == INPUT_S;
  });
  stateMachine.AddTransition(ALARMA, INICIO, []() {
    return input == INPUT_T;
  });
  stateMachine.AddTransition(INICIO, BLOQUEADO, []() {
    return input == INPUT_S;
  });
  stateMachine.AddTransition(BLOQUEADO, INICIO, []() {
    return input == INPUT_T;
  });

  stateMachine.SetOnEntering(INICIO, Inicio);
  stateMachine.SetOnLeaving(INICIO, salir_Inicio);
  stateMachine.SetOnEntering(BLOQUEADO, Bloqueado);
  stateMachine.SetOnLeaving(BLOQUEADO, salir_Bloqueado);
  stateMachine.SetOnEntering(MONITOREO_AMBIENTAL, Monitoreo);
  stateMachine.SetOnLeaving(MONITOREO_AMBIENTAL, salir_Monitoreo);
  stateMachine.SetOnEntering(MONITOR_EVENTOS, Eventos);
  stateMachine.SetOnLeaving(MONITOR_EVENTOS, salir_Eventos);
  stateMachine.SetOnEntering(ALARMA, Alarma);
  stateMachine.SetOnLeaving(ALARMA, salir_Alarma);

  stateMachine.SetState(INICIO, false, true);
}

/**
 * @brief Bucle principal del programa
 * 
 * Gestiona:
 * - Entrada del teclado
 * - Control de LED y zumbador en estados de alarma/bloqueo
 * - Actualización de tareas asíncronas
 * - Actualizaciones de la máquina de estados
 */
void loop() {
  leer_teclado();
  if (stateMachine.GetState() == ALARMA || stateMachine.GetState() == BLOQUEADO) {
    MantenerLed(ledIntervalo);
  }
  if (stateMachine.GetState() == ALARMA) {
    MantenerBuzzer();
  }
  TaskTemperatura.Update();
  TaskLuz.Update();
  TaskInfraRojo.Update();
  TaskMonitoreoAmbiental.Update();
  TaskMonitorEventos.Update();
  TaskBloqueoTiempo.Update();

  stateMachine.Update();
}