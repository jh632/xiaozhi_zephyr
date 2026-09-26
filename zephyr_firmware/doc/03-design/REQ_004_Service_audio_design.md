# 音频 service 模块设计

需求: REQ_004_Service_audio

## 1. 模块职责

音频 service 是小智 AI 固件里唯一的音频通路管理者，向 application 层提供四类能力：

| 能力 | 说明 |
|---|---|
| 语音采集上行 | 从麦克风读取 PCM，编码成 Opus 包交给协议层发往服务器 |
| 语音播放下行 | 接收服务器下发的 Opus 包，解码成 PCM 送到扬声器 |
| 唤醒词检测 | 在待机状态下持续检测唤醒词，检测到之后通知 application |
| 提示音播放 | 播放设备内置提示音，与服务器语音共用同一条播放通路 |

模块内部负责 Opus 编解码、声道提取、任务调度与队列管理。application 层只做设备状态判断与协议收发，不直接接触 I2S 与编解码器。设备端不做回声消除，回声消除由服务器承担。

接口划分与队列语义参考上游 xiaozhi-esp32 的 `main/audio/audio_service.h`。

## 2. 分层位置

| 层 | 内容 |
|---|---|
| application | 设备状态机、协议收发、显示与按键处理 |
| audio service | 采集任务、播放任务、编解码任务；Opus 编解码；唤醒词调度；队列管理 |
| Zephyr 驱动 | audio codec 接口、I2S 接口 |
| 硬件 | 麦克风、编解码器、功放与扬声器 |

音频 service 通过 Zephyr 的 audio codec 接口访问编解码器，器件句柄来自设备树，更换编解码器芯片不需要改动本模块，驱动按同样的接口为各款编解码器提供。

音频参数全链路固定为 16 kHz、16 位，单声道数据放在两个 I2S 槽位里。Opus 帧长 60 毫秒，与服务器协议一致，编码使用较低复杂度参数。

## 3. 数据链路

```mermaid
flowchart LR
  MIC[麦克风] --> ENG[音频引擎] --> EQ[编码队列] --> ENC[Opus 编码] --> SQ[发送队列] --> SRV[服务器]
  SRV --> DQ[解码队列] --> DEC[Opus 解码] --> PQ[播放队列] --> SPK[扬声器]
```

上行：采集任务从 I2S 读取 10 毫秒一块的 PCM，提取单声道数据后送入音频引擎；引擎输出的 60 毫秒整帧进入编码队列，由编解码任务编码成 Opus 包进入发送队列，协议层从发送队列取包发往服务器。

下行：协议层把收到的 Opus 包推入解码队列，编解码任务解码成 PCM 后进入播放队列，播放任务取出后写入 I2S 与 codec 器件。

提示音从同一条下行链路进入：`audio_service_sound_play()` 把内置素材按 60 毫秒拆帧后推入解码队列，与服务器语音共用解码任务与播放任务。

## 4. 任务划分

| 任务 | 职责 | 阻塞方式 |
|---|---|---|
| audio_input | 读取 I2S 采集数据，送音频引擎 | 信号量等待采集使能 |
| audio_output | 从播放队列取 PCM 写入 I2S | 队列等待 |
| opus_codec | 上行编码、下行解码 | 队列等待 |

三个任务用 `k_thread` 静态创建，栈用 `K_THREAD_STACK_DEFINE` 分配，队列用 `k_msgq`（固定容量，运行期不分配内存）。

采集通路与播放通路按需打开：`audio_service_voice_processing_enable(true)` 与播放队列非空时打开对应的 codec 通路，两条通路都空闲 15 秒后关闭，降低待机功耗。

## 5. 对外接口

### 5.1 生命周期

| 接口 | 职责 |
|---|---|
| `audio_service_init()` | 配置 I2S 与 codec 器件，创建队列、Opus 编解码器与任务 |
| `audio_service_start()` | 启动三个任务 |
| `audio_service_stop()` | 停止任务，清空全部队列 |

I2S 配置顺序固定为先播放方向、再采集方向，采集方向沿用同一套帧同步。

### 5.2 上行采集

| 接口 | 职责 |
|---|---|
| `audio_service_voice_processing_enable(bool enable)` | 打开或关闭采集上行 |
| `audio_service_send_packet_pop(struct audio_service_packet_t *packet)` | 取走一个编码后的 Opus 包，返回非零表示当前无数据 |

### 5.3 下行播放

| 接口 | 职责 |
|---|---|
| `audio_service_decode_packet_push(const struct audio_service_packet_t *packet, bool wait)` | 把 Opus 包送入解码队列；`wait` 为真时队列满则等待，为假时队列满立即返回失败 |
| `audio_service_decoder_reset(void)` | 打断当前播放：递增播放代号，清空解码队列与播放队列 |
| `audio_service_playback_idle_check(void)` | 播放链路是否排空 |
| `audio_service_idle_check(void)` | 上行编码队列与播放链路是否全部空闲 |

`audio_service_decoder_reset()` 用播放代号实现打断：正在解码的包完成之后发现代号已变化，直接丢弃，不会在打断之后继续出声。

### 5.4 唤醒词

| 接口 | 职责 |
|---|---|
| `audio_service_wake_word_enable(bool enable)` | 打开或关闭唤醒词检测 |
| `audio_service_wake_word_encode(void)` | 把唤醒瞬间缓存的音频编码 |
| `audio_service_wake_word_packet_pop(struct audio_service_packet_t *packet)` | 取走唤醒词音频包，作为会话首个数据包发往服务器 |
| `audio_service_last_wake_word_get(void)` | 最近一次检测到的唤醒词文本 |
| `audio_service_wake_word_release(void)` | 释放唤醒模型占用的内存 |

### 5.5 提示音

| 接口 | 职责 |
|---|---|
| `audio_service_sound_play(enum audio_service_sound sound)` | 播放指定的内置提示音 |

### 5.6 回调

| 回调 | 触发时机 |
|---|---|
| `send_queue_available` | 发送队列有新包，application 据此发起发送 |
| `wake_word_detected` | 检测到唤醒词 |
| `playback_drained` | 播放链路排空，application 据此启动延迟的聆听 |
| `playback_progress` | 播放进度变化，用于通知类播放的字幕同步 |

### 5.7 数据结构与接口原型

```c
struct audio_service_packet_t {
	uint32_t sample_rate;
	uint32_t frame_duration;
	uint32_t playback_id;
	uint32_t media_position_ms;
	uint8_t *payload;
	size_t payload_size;
};

struct audio_service_callbacks_t {
	void (*send_queue_available)(void *user_data);
	void (*wake_word_detected)(void *user_data, const char *wake_word);
	void (*playback_drained)(void *user_data);
	void (*playback_progress)(void *user_data, uint32_t playback_id, uint32_t position_ms);
};

int audio_service_init(void);
int audio_service_start(void);
int audio_service_stop(void);

int audio_service_voice_processing_enable(bool enable);
int audio_service_send_packet_pop(struct audio_service_packet_t *packet);

int audio_service_decode_packet_push(const struct audio_service_packet_t *packet, bool wait);
int audio_service_decoder_reset(void);
bool audio_service_playback_idle_check(void);
bool audio_service_idle_check(void);

int audio_service_wake_word_enable(bool enable);
int audio_service_wake_word_encode(void);
int audio_service_wake_word_packet_pop(struct audio_service_packet_t *packet);
const char *audio_service_last_wake_word_get(void);
int audio_service_wake_word_release(void);

int audio_service_sound_play(enum audio_service_sound sound);

int audio_service_callbacks_set(const struct audio_service_callbacks_t *callbacks);
```

包结构描述数据本身，不持有缓冲的所有权。上行取包时由调用者提供接收缓冲，模块把包内容拷贝进去，返回时填好采样率、帧长与实际长度。下行送包时由调用者提供包内容，模块在入队时拷贝进内部缓冲，调用返回之后调用者的缓冲可以释放。

## 6. 队列与容量

| 队列 | 容量 | 满时策略 |
|---|---|---|
| 编码队列 | 2 帧 | 丢弃最旧帧 |
| 发送队列 | 40 包（约 2400 毫秒） | 丢弃最旧包 |
| 解码队列 | 20 包（约 1200 毫秒） | 按 `wait` 参数等待或拒绝 |
| 播放队列 | 2 帧 | 编解码任务暂停解码 |

上行数据是实时的，队列满时丢弃最旧的帧，绝不阻塞采集任务。下行 `wait` 参数区分两类来源：提示音与通知必须完整播放，网络语音在拥塞时可以丢弃。

## 7. 与设备状态机的配合

| 设备状态 | 上行采集 | 唤醒词检测 |
|---|---|---|
| Idle | 关闭 | 打开 |
| Listening | 打开 | 关闭 |
| Speaking | 关闭 | 关闭 |
| Notifying | 关闭 | 关闭 |

Speaking 状态关闭唤醒词检测，设备不支持在播报过程中用唤醒词打断。打断播报由按键或服务器指令触发，触发时调用 `audio_service_decoder_reset()` 清空播放链路。

状态切换由 application 层发起，音频 service 只提供上表中的开关接口与状态查询接口，自身不感知设备状态。

## 8. 依赖

| 依赖 | 用途 | 当前状态 |
|---|---|---|
| Opus 编解码库 | 上行编码与下行解码 | 工作区未引入，需要作为 Zephyr 模块加入 |
| 唤醒词推理与模型 | 唤醒词检测 | 设备端唤醒词引擎与模型，需要单独引入 |
| 提示音素材 | 提示音播放 | 预转成 16 kHz 单声道 PCM 存入 flash |
