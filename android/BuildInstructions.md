# Building the Ice for Android Test Suite

This directory contains an Android Studio project for the Ice test suite. For
ease of development and testing, this project also builds a subset of the Ice
JAR files. This is not necessary for your own projects as it considerably
complicates the project configuration.

Building [Ice for Java](../java/BuildInstructions.md) is the only way to build
all of the Ice JAR files from source. The JAR files produced by the Ice for Java
build fully support Android. If you prefer, our [binary distributions][1]
include pre-compiled JAR files.

## Build Requirements

### Android Development Tools

Building any Ice application for Android requires Android Studio and the Android
SDK build tools. We tested with the following components:

- Android Studio 1.2.1
- Android SDK Build-tools 21.1.1

Ice requires at minimum API level 17:

- Android 4.2.2 (API17)

If you want to target a later version of the Android API level for the test
suite, edit `gradle.properties` and change the following variables:

    ice_compileSdkVersion
    ice_minSdkVersion
    ice_targetSdkVersion

### Slice to Java Translator

To build this project you'll need the Slice to Java translator, which generates
Java code from Slice definitions. The translator is written in C++. If you have
a suitable C++ development environment, you can build [Ice for C++](../cpp)
yourself. Otherwise, you can obtain the translator by installing a
[binary distribution][1].

The project's Gradle-based build system will automatically search for the
translator in this repository and in the default installation directories used
by the binary distributions for our supported platforms.

### Bzip2 Compression

Ice for Android supports protocol compression using the bzip2 classes included
with Apache Ant or available separately from [kohsuke.org]().

The Maven package id for the bzip2 JAR file is as follows:

    groupId=org.apache.tools, version=1.0, artifactId=bzip2

You must add the bzip2 classes to your project to enable protocol compression.

> *These classes are a pure Java implementation of the bzip2 algorithm and
therefore add significant latency to Ice requests.*

## Building the Project

Follow these steps to import the Ice for Android project into Android Studio:

1. Start Android Studio
2. Select Open Project
3. Navigate to the android subdirectory
4. If presented with an "Import Project from Gradle" dialog, select
   "Use default gradle wrapper" and press OK 

The Android Studio project contains a `testApp` application for the Ice test
suite. To run the application, select it in the configuration pull down and run
it.

[1]: https://zeroc.com/downloads/ice
