#
/*-
 * SPDX-License-Identifier: BSD 2-Clause License
 *
 * Copyright (c) 2026, KusaReMKN
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <cstdint>
#include <cstring>
#include <deque>

#include <TimerTC3.h>

#define PIN_AUX	(D1)
#define PIN_MOD	(D2)

#define TIC	100

static volatile bool resetSerial1;

void
modHandler(void)
{
	resetSerial1 = true;
}

void
setup(void)
{
	pinMode(PIN_AUX, INPUT);
	pinMode(PIN_MOD, INPUT_PULLUP);

	while (!Serial)
		delayMicroseconds(TIC);
	Serial.begin(115200);

	while (!Serial1)
		delayMicroseconds(TIC);
	Serial1.begin(9600);

	attachInterrupt(digitalPinToInterrupt(PIN_MOD), modHandler, CHANGE);

	while (digitalRead(PIN_AUX) == LOW)
		delayMicroseconds(TIC);

	resetSerial1 = true;
}

void
loop(void)
{
	// LoRa との通信を再設定する
	if (resetSerial1) {
		Serial1.end();
		Serial1.begin(digitalRead(PIN_MOD) == HIGH ? 9600 : 115200);
		resetSerial1 = false;
	}

	// パケットの到来を待つ
	while (Serial1.available() == 0)
		delayMicroseconds(TIC);

	// パケットの長さを受信する
	int len;
	uint8_t buf[256];
	buf[0] = len = Serial1.read();

	// パケット全体を受信する
	for (int i = 1; i <= len; i++) {
		while (Serial1.available() == 0)
			delayMicroseconds(TIC);
		buf[i] = Serial1.read();
	}

	// これは IPv4 パケットか？
	uint16_t ethertype;
	ethertype = (buf[6] << 8 | buf[7]);
	if (ethertype != 0x0800) {
		Serial.println("It is not IPv4 Packet");
		Serial.printf("%x\n", ethertype);
		return;
	}

	uint8_t *iphead;
	iphead = &buf[8];

	// これは自分宛か？
#define IPADDR(a, b, c, d) \
	((((a) << 8 | (b)) << 8 | (c)) << 8 | (d))
#define MYADDR	IPADDR(10, 5, 2, 50)
	uint32_t destaddr;
	destaddr = IPADDR(iphead[16], iphead[17], iphead[18], iphead[19]);
	if (destaddr != MYADDR) {
		Serial.println("It is not packet for me");
		return;
	}

	// これは ICMP パケットか？
	uint8_t proto;
	proto = iphead[9];
	if (proto != 0x01) {
		Serial.println("It is not ICMP Packet");
		return;
	}

	uint8_t *icmphead;
	icmphead = &iphead[20];

	// これは Echo Request Message か？
	uint8_t icmptype;
	icmptype = icmphead[0];
	if (icmptype != 8) {
		Serial.println("It is not Echo Request Message");
		return;
	}

	// うわ、仕事だ
	Serial.println("ICMP Echo Request Message!!");

	// 宛先と送信元とを入れ替える
	// チェックサムはかわらないので計算しなくて良い！（うわ）
	uint8_t tmpaddr[4];
	std::memcpy(tmpaddr, &iphead[12], 4);
	std::memcpy(&iphead[12], &iphead[16], 4);
	std::memcpy(&iphead[16], tmpaddr, 4);

	// Echo Reply Message にする
	icmphead[0] = 0;

	// ズルしてチェックサムを求める
	uint32_t chksum;
	chksum = (icmphead[2] << 8 | icmphead[3]);
	chksum = (~chksum & 0xFFFF);
	chksum += (~0x0800 & 0xFFFF);
	while (chksum >> 16)
		chksum = (chksum & 0xFFFF) + (chksum >> 16);
	icmphead[3] = (chksum & 0xFF);
	icmphead[2] = (chksum >> 8 & 0xFF);

	// パケットを再送信する
	buf[0] = buf[1] = 0xFF;
	buf[2] = 0x07;
	buf[3] = buf[4] = 0xFF;
	buf[5] = 0x07;

	// 送信可能になるまで待つ
	while (digitalRead(PIN_AUX) != HIGH)
		delayMicroseconds(TIC);
	delay(50);

	// チェックサム再計算
	buf[len] = 0;
	for (int i = 0; i < len; i++) {
		Serial1.write(buf[i]);
		buf[len] ^= buf[i];
	}
	Serial1.write(buf[len]);
}
