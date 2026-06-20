@echo off
REM CamperNode — Einrichtungs-Assistent im Browser (kein Python noetig)
chcp 65001 >nul 2>&1
title CamperNode Einrichtung
echo.
echo  CamperNode Einrichtungs-Assistent (Firmware v0.2.0)
echo  Die Schritt-fuer-Schritt-Anleitung oeffnet sich im Browser.
echo  Hinweis: GPIO 16 und 17 sind reserviert (UART-Monitor).
echo.
start "" "%~dp0einrichtung.html"
