#include "PublishSpriteSheet.h"
#include "SpritePackerProjectFile.h"
#include "SpriteAtlas.h"
#include "PListSerializer.h"
#include "optipng.h"
#include <QMessageBox>

QMap<QString, QString> PublishSpriteSheet::_formats;

QJSValue jsValue(QJSEngine& engine, const QRect& rect) {
    QJSValue value = engine.newObject();
    value.setProperty("x", rect.left());
    value.setProperty("y", rect.top());
    value.setProperty("width", rect.width());
    value.setProperty("height", rect.height());
    return value;
}

QJSValue jsValue(QJSEngine& engine, const QSize& size) {
    QJSValue value = engine.newObject();
    value.setProperty("width", size.width());
    value.setProperty("height", size.height());
    return value;
}

QJSValue jsValue(QJSEngine& engine, const QPoint& point) {
    QJSValue value = engine.newObject();
    value.setProperty("x", point.x());
    value.setProperty("y", point.y());
    return value;
}

void JSConsole::log(QString msg) {
    qDebug() << "js:"<< msg;
}

bool PublishSpriteSheet::publish(const QString& filePath, const QString& format, const SpriteAtlas& spriteAtlas, bool errorMessage) {
    QJSEngine engine;

    auto it_format = _formats.find(format);
    if (it_format == _formats.end()) {
        QString errorString = QString("Not found script file for [%1] format").arg(format);
        qDebug() << errorString;
        if (errorMessage) QMessageBox::critical(NULL, "Export script error", errorString);
        return false;
    }

    QString scriptFileName = it_format.value();
    QFile scriptFile(scriptFileName);
    if (!scriptFile.open(QIODevice::ReadOnly)) {
        qDebug() << "File [" << scriptFileName << "] not found!";
        return false;
    }

    QTextStream stream(&scriptFile);
    QString contents = stream.readAll();
    scriptFile.close();

    // add console object
    JSConsole console;
    QJSValue consoleObj = engine.newQObject(&console);
    engine.globalObject().setProperty("console", consoleObj);

    // evaluate export plugin script
    qDebug() << "Run script...";
    QJSValue result = engine.evaluate(contents, scriptFileName);
    if (result.isError()) {
        QString errorString = "Uncaught exception at line " + result.property("lineNumber").toString() + " : " + result.toString();
        qDebug() << errorString;
        if (errorMessage) QMessageBox::critical(NULL, "Export script error", errorString);
        return false;
    }

    if (engine.globalObject().hasOwnProperty("exportSpriteSheet")) {
        QJSValueList args;
        args << QJSValue(filePath);
        args << QJSValue(filePath + ".png");

        // collect sprite frames
        QJSValue spriteFramesValue = engine.newObject();
        auto it_f = spriteAtlas.spriteFrames().cbegin();
        for (; it_f != spriteAtlas.spriteFrames().cend(); ++it_f) {
            QJSValue spriteFrameValue = engine.newObject();
            spriteFrameValue.setProperty("frame", jsValue(engine, it_f.value().mFrame));
            spriteFrameValue.setProperty("offset", jsValue(engine, it_f.value().mOffset));
            spriteFrameValue.setProperty("rotated", it_f.value().mRotated);
            spriteFrameValue.setProperty("sourceColorRect", jsValue(engine, it_f.value().mSourceColorRect));
            spriteFrameValue.setProperty("sourceSize", jsValue(engine, it_f.value().mSourceSize));

            spriteFramesValue.setProperty(it_f.key(), spriteFrameValue);
        }
        args << QJSValue(spriteFramesValue);

        // run export
        QJSValue exportSpriteSheet = engine.globalObject().property("exportSpriteSheet");
        result = exportSpriteSheet.call(args);

        if (result.isError()) {
            QString errorString = "Uncaught exception at line " + result.property("lineNumber").toString() + " : " + result.toString();
            qDebug() << errorString;
            if (errorMessage) QMessageBox::critical(NULL, "Export script error", errorString);
            return false;
        } else {
            // write image
            spriteAtlas.image().save(filePath + ".png");
            // TODO: optimize in QThread and enable/disable on preferences
            //optimizePNG(filePath + ".png");

            // write data
            if (!result.hasProperty("data") || !result.hasProperty("format")) {
                QString errorString = "Script function must be return object: {data:data, format:'plist|json|other'}";
                qDebug() << errorString;
                if (errorMessage) QMessageBox::critical(NULL, "Export script error", errorString);
                return false;
            } else {
                QJSValue data = result.property("data");
                QString format = result.property("format").toString();
                QFile file(filePath + "." + format);
                file.open(QIODevice::WriteOnly | QIODevice::Text);
                QTextStream out(&file);
                if (format == "plist") {
                    out << PListSerializer::toPList(data.toVariant());
                } else {
                    out << data.toString();
                }
            }
        }

    } else {
        qDebug() << "Not found global exportSpriteSheet function!";
        if (errorMessage) QMessageBox::critical(NULL, "Export script error", "Not found global exportSpriteSheet function!");
        return false;
    }

    return true;
}

/** Application-defined printf callback **/
static void app_printf(const char *fmt, ...) {
//    va_list args;
//    va_start(args, fmt);
//    printf(fmt, args);
//    va_end(args);
}

/** Application-defined control print callback **/
static void app_print_cntrl(int cntrl_code) {
    // TODO: implement
}

/** Application-defined progress update callback **/
static void app_progress(unsigned long current_step, unsigned long total_steps) {
    // TODO: implement
    //qDebug("current_step: %d (total_steps: %d)\n", (int)current_step, (int)total_steps);
}

/** Panic handling **/
static void panic(const char *msg) {
    /* Print the panic message to stderr and terminate abnormally. */
    qCritical("** INTERNAL ERROR: %s", msg);
}

bool PublishSpriteSheet::optimizePNG(const QString& fileName) {
    /* Initialize the optimization engine. */
    opng_options options;
    memset(&options, 0, sizeof(options));
    options.optim_level = -1;
    options.interlace = -1;

    opng_ui ui;
    ui.printf_fn      = app_printf;
    ui.print_cntrl_fn = app_print_cntrl;
    ui.progress_fn    = app_progress;
    ui.panic_fn       = panic;
    if (opng_initialize(&options, &ui) != 0) {
        qCritical() << "Can't initialize optimization engine";
        return false;
    }

    if (opng_optimize(fileName.toStdString().c_str()) != 0) {
        return false;
    }

    /* Finalize the optimization engine. */
    if (opng_finalize() != 0) {
        qCritical() << "Can't finalize optimization engine";
    }
    return true;
}