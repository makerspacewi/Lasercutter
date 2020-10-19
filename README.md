# Großer Laser Cutter (GLC)
Control Lasercutter

Leider wurde der Aufbau vom GLC nicht dokumentiert, aus diesem Grund fehlt hier der Verlauf des Aufbaus über die letzten Jahre.
![Relais_1](doc/IMG_20201015_221211.jpg)


[Zu den Schaltbildern](doc/Schaltpläne_gr_LasercutterV6.pdf)<br>

<br>
<h1>Notwendige Arbeiten am großen Laser Cutter</h1> <br>

19.10.2020
So - am Dienstag den 13.10.2020 hat sich das Technik-Team die Angelegenheit angeschaut und festgestellt, dass noch einige Sicherheitsrelevante Änderungen realisiert werden müssen.

Der Technik-Vorstand hat festgelegt, dass keine Mitglieder für den GLC freigegeben werden dürfen, bis die im folgenden beschriebenen Änderungen ausgeführt sind.

		
ACHTUNG - MOMENTAN sind die Sicherheitsschalter an den beiden Klappen NICHT verdrahtet!
Die NOT-AUS Schalter sollen/dürfen nicht zum Aus-/Einschalten des GLC genutzt werden!
Der Absauglüfter muss manuell vor dem LASERN nach Vorgabe eingeschaltet werden.

<h2>Thema Absauglüfter</h2>
Der momentane Absauglüfter hat einen Wechselstrommotor, dessen Drehzahl sich nicht regeln läßt.
Dieser Absauglüfter ist extrem laut und hat schon zu Beschweden von Nachbarn geführt.
Momentane Behelfslösung vom Laser Cutter Team ist, den Lüfter ‘normal’ zu starten und dann
mittels Phasenanschnittregelung über seine Eigenreibung die Drehzahl zu reduzieren.
Leider lässt sich dieser Prozess nicht automatisieren.

Das Technikteam hat dazu 2 Möglichkeiten evaluiert:

- einen elektronisch regelbaren	Wechselrichter besorgen und damit den Wechselstrommotor regeln	
- bevorzugte Variante sind Lüfter mit Gleichstrommotoren (von Automobilradiatoren) einzusetzen,
die mittels PWM auch elektronisch geregelt werden können. Diese Regelung ist bereits
für den Kühlwasserlüfter in Betrieb.

Bei einer Diskussion mit Brani wurden wir uns einig, dass mehrere Lüfter im Absaugrohr
installiert werden sollen.

<h2>Vorschlag Technik Team zum Umbau des GLC</h2>

- Die 4 NOT-AUS Schalter - wenn ausgelöst - öffnen ein Relais, dass die Ausgangsspannung der beiden Motornetzteilen unterbricht, die für die Achsensteuerung zuständig sind.
Desweiteren wird 'NOT-AUS' an den Controller gemeldet, der den Laser-Controller deaktiviert.

- der Stecker des GLC bleibt zukünftig immer in der Steckdose! Ein kleines Netzteil
versorgt ständig die GLC Zugangs- & Sicherheits Controller, die nur nach erfolgreichem ‘einloggen’ die Netzspannungen an alle Netzteile anlegt und nur dann den CO2-Laser (Controller) frei gibt wenn

		- die Sicherheitsschalter geschlossen sind
		- kein Not-Aus-Schalter aktiviert ist
		- die Rücklauftemperatur des Kühlwassers kleiner Maximaltemperatur ist
		- die Flussgeschwindigkeit des Kühlwassers größer dem Grenzwert ist
		- und ein registriertes Mitglied eingeloggt ist


- Nach getaner Arbeit loggen sich die Mitglieder - wie bei den anderen Maschinen - wieder aus und alle Netzteile werden vom Netz getrennt.

- Um die Absaugung automatisiert einzuschalten, müssen diese vorher entsprechend umgebaut werden.
