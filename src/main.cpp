#include <cstdint>
#include <cstddef>
#include <iostream>
#include <chrono>
#include <stop_token>
#include <thread>
#include <condition_variable>

#include <drogon/drogon.h>
#include "scraper.h"
#include "utils.h"

#include "spdlog/spdlog.h"
#include <gst/gst.h>

// так с чего начать?
// нужно научиться принимать запрос и отдавать какие то данные через сипипи
// начнем с базы: hello world
// капец это говно собрать невозможно
// челиксы предлагают быстренько написать что нибудь на boost beast
// но там типа все слишком лоу лвл

// короч для реддита что можно сделать? наверное нужно вот как:
// периодически бегаем скраппим посты с реддита, то есть
// скачиваем картинки, скачиваем видосы, кладем их в папку на диске
// проверяем чтобы их было не слишком много
// по запросу отправляем мем как файл
// скрапим в соседнем потоке

// короче говоря пока так как есть привести в порядок
// собрать нормальный проект
// попробовать сделать докер
// а уже после заняться видосами
// так что тут вообще имеет смысл?
// наверное один метод максимум

// так все таки какой функционал тут делать?
// скрапить каждое некоторое время мемы с реддита
// собирать все файлы в один массив

// как сделать scraper_settings глобальным?
// как сделать scraper глобальным?

using namespace drogon;

const std::string_view settings_path = "./scraper.json";

// надо посмотреть как это дело будет работать с одним потоком
int main(int argc, char *argv[]) {
  gst_init (&argc, &argv);

  auto sets = utility::scraper_settings_construct();
  try {
    sets = utility::parse_json(std::string(settings_path));
  } catch(const std::exception &e) {
    spdlog::warn("Error '{}'. Skip. Using default one", e.what());
    // сохранить json на диск
    auto buffer = create_json(sets);
    utility::write_file(settings_path, buffer);
  }

  const uint16_t port = sets.port;
  const uint16_t log_level = sets.log_level;

  {
    utility::global g;
    g.init_scraper(4, sets.folder);
    g.init_settings(std::move(sets));
  }

  std::stop_source stop_source;
  std::jthread scraper_thread([] (std::stop_token stoken) {
    utility::scraper_run(stoken);
  }, stop_source.get_token());

  app().setLogPath("./")
       .setLogLevel(static_cast<trantor::Logger::LogLevel>(log_level)) //  trantor::Logger::kWarn
       .addListener("0.0.0.0", port)
       .setThreadNum(8) // имеет смысл брать в рантайме
       //.enableRunAsDaemon()
       .run();

  stop_source.request_stop();

  return 0;
}

// вообще помимо реддита есть идейка что можно сделать из небольшого проекта но полезного
// маленький управлятор РТСП потоком
// принимает в конфиге 1 ртсп поток и с ним взаимодействует
// что может делать? 
// 1) выводить данные об этом потоке (дополнительные данные заданные пользователем + информацию о потоке)
// 2) записывать архив и управлять им
// 3) выводить поток или архив в браузер и быть прокси для этого потока (желательно еще уметь менять качество)
// все это в обертке рестфул апи + видимо вебсокеты

// полный список фичей:
// 1.1 сохранять какую то информацию об конкретно этом потоке
// 1.2 вести лог
// 1.3 подключаться к потоку по запросу, наверное тут же нужно будет указать дополнительную ссылку
// 1.4 обрабатывать информацию о потоке и например чистить поток от звука
// 1.5 уметь правильно реагировать на ситуации обрыва соединения и проч
// 1.6 отдавать картинку по запросу
// 2.1 подключившись к ссылке записывать с нее архив на диск порционно
// 2.2 уметь считать архив и понимать сколько места занято + уметь работать в каком то ограниченном объеме
// 2.3 отдавать информацию об архиве 
// 2.4 стримить сам архив 
// 2.5 удалять часть архива
// 3.1 взаимодействовать с потоком и с разными ситуациями
// 3.1.1 отдавать преобразованный поток в браузер
// 3.1.2 считать и отдать преобразованный архив в браузер
// 3.1.3 проксировать ртсп
// 3.2 апи для всего выше + изменения качества видеопотока
// ???

// апи будет выглядеть как то так:
// /info - отдаем всю информацию которая есть
// /info/name - отдаем имя в формате по умолчанию (по умолчанию json)
// /info/name/raw - отдаем имя просто строкой 
// /info/name/json - отдаем имя в json
// ... и так далее с информацией
// /ping - информация о состоянии потока
// /log?type= - возвращаем лог по типу и наверное еще время можно указать
// /picture?width=height=format= - отдаем картинку (по умолчанию jpeg) + необязательные опции чтобы ее преобразовать как надо
// /archive - нужно отдать общую информацию о архиве
// + нужно уметь отдавать информацию об архиве за какое то время
// + нужно уметь стримить архив и скачивать его
// + наверное метод DELETE по времени удаляет архив

// с точки зрения приложения у нас будет сделано все примерно вот так:
// существует ряд источников + ряд получателей
// источники подключаются как только у них появляются получатели
// что сама камера что архив генерируют пакеты по времени то есть
// источник сгенерил кусок видео, получатель его съел и опять находится в ожидании
// единственное отличие что для скачивания мы бы хотели по возможности получить все сразу
// я бы просто разделил скачивание это отдельная операция
// источников потенциально может быть много - много одновременных подключений на скачивание архива
// источник представляет из себя объект который считывает данные из rtsp потока или с диска
// источник возвращает информацию о потоке а так же готов или не готов пакет с данными
// приемник должен дать понять КАК бы он хотел эти данные забрать
// так тут встают главные сложности - нужно выкинуть список поддерживаемых форматов

// так короче я посмотрел на преобразование потоков в ффмпеге и это жесть
// но при этом как будто даже можно из этого что то получить
// по идее меня интересуют 3 настройки: размер кадра, фпс, качество
// желательно уметь отдавать поток с заданным размером битрейта
// наверное у меня будет занято два порта: один http запросы, второй rtsp

// капец чтобы сделать хороший РТСП сервер нужно изрядно попотеть 