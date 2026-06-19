@echo off
REM CamperNode — Einrichtungs-Assistent im Browser (kein Python noetig)
chcp 65001 >nul 2>&1
title CamperNode Einrichtung
echo.
echo  CamperNode Einrichtungs-Assistent
echo  Die Schritt-fuer-Schritt-Anleitung oeffnet sich im Browser.
echo.
start "" "%~dp0einrichtung.html"
