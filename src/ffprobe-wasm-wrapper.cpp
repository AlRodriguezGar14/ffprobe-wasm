#include <cmath>
#include <emscripten.h>
#include <emscripten/bind.h>
#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace emscripten;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/bprint.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
};

const std::string c_avformat_version() {
  return AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

const std::string c_avcodec_version() {
  return AV_STRINGIFY(LIBAVCODEC_VERSION);
}

const std::string c_avutil_version() { return AV_STRINGIFY(LIBAVUTIL_VERSION); }

typedef struct Tag {
  std::string key;
  std::string value;
} Tag;

typedef struct ProbeRational {
  int num;
  int den;
} ProbeRational;

typedef struct Disposition {
  int default_flag;
  int dub;
  int original;
  int comment;
  int lyrics;
  int karaoke;
  int forced;
  int hearing_impaired;
  int visual_impaired;
  int clean_effects;
  int attached_pic;
  int timed_thumbnails;
  int captions;
  int descriptions;
  int metadata;
  int dependent;
  int still_image;
} Disposition;

static ProbeRational make_rational(AVRational v) {
  ProbeRational r = {v.num, v.den};
  return r;
}

static std::string rational_to_string(int num, int den, char sep) {
  std::ostringstream oss;
  oss << num << sep << den;
  return oss.str();
}

static double ts_seconds(int64_t ts, AVRational time_base) {
  if (ts == AV_NOPTS_VALUE || time_base.den == 0) {
    return 0.0;
  }
  return ts * av_q2d(time_base);
}

static const char *field_order_name(enum AVFieldOrder fo) {
  switch (fo) {
  case AV_FIELD_PROGRESSIVE:
    return "progressive";
  case AV_FIELD_TT:
    return "tt";
  case AV_FIELD_BB:
    return "bb";
  case AV_FIELD_TB:
    return "tb";
  case AV_FIELD_BT:
    return "bt";
  default:
    return NULL;
  }
}

static std::string str_or_empty(const char *s) {
  return s ? std::string(s) : std::string();
}

static std::string channel_layout_string(int channels, uint64_t layout) {
  if (channels <= 0)
    return std::string();
  char buf[128] = {0};
  av_get_channel_layout_string(buf, sizeof(buf), channels, layout);
  return std::string(buf);
}

static Disposition fill_disposition(int d) {
  Disposition out = {};
  out.default_flag = !!(d & AV_DISPOSITION_DEFAULT);
  out.dub = !!(d & AV_DISPOSITION_DUB);
  out.original = !!(d & AV_DISPOSITION_ORIGINAL);
  out.comment = !!(d & AV_DISPOSITION_COMMENT);
  out.lyrics = !!(d & AV_DISPOSITION_LYRICS);
  out.karaoke = !!(d & AV_DISPOSITION_KARAOKE);
  out.forced = !!(d & AV_DISPOSITION_FORCED);
  out.hearing_impaired = !!(d & AV_DISPOSITION_HEARING_IMPAIRED);
  out.visual_impaired = !!(d & AV_DISPOSITION_VISUAL_IMPAIRED);
  out.clean_effects = !!(d & AV_DISPOSITION_CLEAN_EFFECTS);
  out.attached_pic = !!(d & AV_DISPOSITION_ATTACHED_PIC);
  out.timed_thumbnails = !!(d & AV_DISPOSITION_TIMED_THUMBNAILS);
  out.captions = !!(d & AV_DISPOSITION_CAPTIONS);
  out.descriptions = !!(d & AV_DISPOSITION_DESCRIPTIONS);
  out.metadata = !!(d & AV_DISPOSITION_METADATA);
  out.dependent = !!(d & AV_DISPOSITION_DEPENDENT);
  out.still_image = !!(d & AV_DISPOSITION_STILL_IMAGE);
  return out;
}

static std::string snap_aspect_label(int width, int height) {
  if (width <= 0 || height <= 0)
    return std::string();
  double r = (double)width / (double)height;

  struct AR {
    const char *label;
    double ratio;
  };

  static const AR table[] = {
      {"1:1", 1.0},         {"5:4", 5.0 / 4.0},   {"4:3", 4.0 / 3.0},
      {"3:2", 3.0 / 2.0},   {"14:9", 14.0 / 9.0}, {"16:10", 16.0 / 10.0},
      {"16:9", 16.0 / 9.0}, {"1.85:1", 1.85},     {"2:1", 2.0},
      {"2.20:1", 2.20},     {"2.35:1", 2.35},     {"2.39:1", 2.39},
      {"21:9", 21.0 / 9.0}, {"9:16", 9.0 / 16.0}, {"2:3", 2.0 / 3.0},
      {"3:4", 3.0 / 4.0},   {"4:5", 4.0 / 5.0},
  };
  const double tol = 0.005;
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
    if (std::fabs(r - table[i].ratio) / table[i].ratio < tol) {
      return std::string(table[i].label);
    }
  }
  return std::string();
}

static void fill_coded_dimensions(int *coded_width, int *coded_height,
                                  const AVStream *stream) {
  *coded_width = 0;
  *coded_height = 0;
  const AVCodecParameters *codecpar = stream->codecpar;
  if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    return;

  const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
  if (!decoder)
    return;

  AVCodecContext *ctx = avcodec_alloc_context3(decoder);
  if (!ctx)
    return;

  if (avcodec_parameters_to_context(ctx, codecpar) >= 0 &&
      avcodec_open2(ctx, decoder, NULL) >= 0) {
    *coded_width = ctx->coded_width > 0 ? ctx->coded_width : ctx->width;
    *coded_height = ctx->coded_height > 0 ? ctx->coded_height : ctx->height;
  }
  avcodec_free_context(&ctx);
}

typedef struct Stream {
  int index;
  int id;
  std::string codec_type;
  std::string codec_name;
  std::string codec_long_name;
  std::string codec_tag_string;
  std::string codec_tag;
  std::string format;
  std::string profile;
  int level;
  int width;
  int height;
  int channels;
  int sample_rate;
  int frame_size;
  std::vector<Tag> tags;
  int coded_width;
  int coded_height;
  int has_b_frames;
  std::string sample_aspect_ratio;
  ProbeRational sample_aspect_ratio_rational;
  std::string display_aspect_ratio;
  ProbeRational display_aspect_ratio_rational;
  std::string display_aspect_ratio_label;
  std::string pix_fmt;
  std::string color_range;
  std::string color_space;
  std::string color_transfer;
  std::string color_primaries;
  std::string chroma_location;
  std::string field_order;
  std::string sample_fmt;
  std::string channel_layout;
  int bits_per_sample;
  int initial_padding;
  std::string r_frame_rate;
  ProbeRational r_frame_rate_rational;
  std::string avg_frame_rate;
  ProbeRational avg_frame_rate_rational;
  std::string time_base;
  ProbeRational time_base_rational;
  int64_t start_pts;
  double start_time;
  int64_t duration_ts;
  double duration;
  int64_t bit_rate;
  int bits_per_raw_sample;
  int64_t nb_frames;
  int extradata_size;
  Disposition disposition;
} Stream;

typedef struct Chapter {
  int id;
  std::string time_base;
  float start;
  float end;
  std::vector<Tag> tags;
} Chapter;

typedef struct FileInfoResponse {
  std::string name;
  float bit_rate;
  float duration;
  std::string url;
  int nb_streams;
  int flags;
  std::vector<Stream> streams;
  int nb_chapters;
  std::vector<Chapter> chapters;
  std::string error;
} FileInfoResponse;

FileInfoResponse get_file_info(std::string filename) {
  av_log_set_level(AV_LOG_QUIET); // No logging output for libav.

  // Initialize response struct with format data.
  FileInfoResponse r;

  FILE *file = fopen(filename.c_str(), "rb");
  if (!file) {
    r.error.assign("cannot open file\n");
    return r;
  }
  fclose(file);

  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext) {
    r.error.assign("could not allocate memory for Format Context");
    return r;
  }

  // Open the file and read header.
  int ret;
  if ((ret = avformat_open_input(&pFormatContext, filename.c_str(), NULL,
                                 NULL)) < 0) {
    r.error.assign(av_err2str(ret));
    return r;
  }

  // Get stream info from format.
  if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
    r.error.assign("could not get stream info");
    return r;
  }

  r.name = pFormatContext->iformat->name;
  r.bit_rate = (float)pFormatContext->bit_rate;
  r.duration = (float)pFormatContext->duration;
  r.url = pFormatContext->url;
  r.nb_streams = (int)pFormatContext->nb_streams;
  r.flags = pFormatContext->flags;
  r.nb_chapters = (int)pFormatContext->nb_chapters;

  // Loop through the streams.
  for (int i = 0; i < pFormatContext->nb_streams; i++) {
    AVStream *pStream = pFormatContext->streams[i];
    AVCodecParameters *pLocalCodecParameters = pStream->codecpar;
    const AVCodecDescriptor *descriptor =
        avcodec_descriptor_get(pLocalCodecParameters->codec_id);
    const char *pix_fmt_name =
        pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO
            ? av_get_pix_fmt_name((AVPixelFormat)pLocalCodecParameters->format)
            : NULL;
    const char *sample_fmt_name =
        pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO
            ? av_get_sample_fmt_name(
                  (AVSampleFormat)pLocalCodecParameters->format)
            : NULL;

    AVRational dar = {0, 1};
    if (pLocalCodecParameters->width > 0 && pLocalCodecParameters->height > 0) {
      AVRational sar = pStream->sample_aspect_ratio;
      if (sar.num <= 0 || sar.den <= 0) {
        sar.num = 1;
        sar.den = 1;
      }
      // Compute Display Aspect Ratio
      av_reduce(&dar.num, &dar.den,
                (int64_t)pLocalCodecParameters->width * sar.num,
                (int64_t)pLocalCodecParameters->height * sar.den, 1024 * 1024);
    }

    Stream stream = {};
    stream.index = pStream->index;
    stream.id = (int)pStream->id;
    stream.codec_type = str_or_empty(
        av_get_media_type_string(pLocalCodecParameters->codec_type));
    stream.codec_name = str_or_empty(
        avcodec_descriptor_get(pLocalCodecParameters->codec_id)
            ? avcodec_descriptor_get(pLocalCodecParameters->codec_id)->name
            : NULL);
    stream.codec_long_name =
        str_or_empty(descriptor ? descriptor->long_name : NULL);
    {
      char tag_buf[AV_FOURCC_MAX_STRING_SIZE] = {0};
      av_fourcc_make_string(tag_buf, pLocalCodecParameters->codec_tag);
      stream.codec_tag_string = std::string(tag_buf);
    }
    {
      std::ostringstream oss;
      oss << "0x" << std::hex << std::setw(8) << std::setfill('0')
          << (uint32_t)pLocalCodecParameters->codec_tag;
      stream.codec_tag = oss.str();
    }
    stream.format = str_or_empty(pix_fmt_name);
    stream.profile = str_or_empty(avcodec_profile_name(
        pLocalCodecParameters->codec_id, pLocalCodecParameters->profile));
    stream.level = (int)pLocalCodecParameters->level;
    stream.width = (int)pLocalCodecParameters->width;
    stream.height = (int)pLocalCodecParameters->height;
    stream.channels = (int)pLocalCodecParameters->channels;
    stream.sample_rate = (int)pLocalCodecParameters->sample_rate;
    stream.frame_size = (int)pLocalCodecParameters->frame_size;

    fill_coded_dimensions(&stream.coded_width, &stream.coded_height, pStream);
    stream.has_b_frames = pLocalCodecParameters->video_delay;
    stream.sample_aspect_ratio_rational =
        make_rational(pStream->sample_aspect_ratio);
    stream.sample_aspect_ratio =
        rational_to_string(pStream->sample_aspect_ratio.num,
                           pStream->sample_aspect_ratio.den, ':');
    stream.display_aspect_ratio_rational = make_rational(dar);
    stream.display_aspect_ratio = rational_to_string(dar.num, dar.den, ':');
    stream.display_aspect_ratio_label = snap_aspect_label(dar.num, dar.den);
    stream.pix_fmt = str_or_empty(pix_fmt_name);
    stream.color_range =
        str_or_empty(av_color_range_name(pLocalCodecParameters->color_range));
    stream.color_space =
        str_or_empty(av_color_space_name(pLocalCodecParameters->color_space));
    stream.color_transfer =
        str_or_empty(av_color_transfer_name(pLocalCodecParameters->color_trc));
    stream.color_primaries = str_or_empty(
        av_color_primaries_name(pLocalCodecParameters->color_primaries));
    stream.chroma_location = str_or_empty(
        av_chroma_location_name(pLocalCodecParameters->chroma_location));
    stream.field_order =
        str_or_empty(field_order_name(pLocalCodecParameters->field_order));
    stream.sample_fmt = str_or_empty(sample_fmt_name);
    stream.channel_layout = channel_layout_string(
        pLocalCodecParameters->channels, pLocalCodecParameters->channel_layout);
    stream.bits_per_sample =
        av_get_bits_per_sample(pLocalCodecParameters->codec_id);
    stream.initial_padding = pLocalCodecParameters->initial_padding;
    stream.r_frame_rate_rational = make_rational(pStream->r_frame_rate);
    stream.r_frame_rate = rational_to_string(pStream->r_frame_rate.num,
                                             pStream->r_frame_rate.den, '/');
    stream.avg_frame_rate_rational = make_rational(pStream->avg_frame_rate);
    stream.avg_frame_rate = rational_to_string(
        pStream->avg_frame_rate.num, pStream->avg_frame_rate.den, '/');
    stream.time_base_rational = make_rational(pStream->time_base);
    stream.time_base =
        rational_to_string(pStream->time_base.num, pStream->time_base.den, '/');
    stream.start_pts = pStream->start_time;
    stream.start_time = ts_seconds(pStream->start_time, pStream->time_base);
    stream.duration_ts = pStream->duration;
    stream.duration = ts_seconds(pStream->duration, pStream->time_base);
    stream.bit_rate = pLocalCodecParameters->bit_rate;
    stream.bits_per_raw_sample = pLocalCodecParameters->bits_per_raw_sample;
    stream.nb_frames = pStream->nb_frames;
    stream.extradata_size = pLocalCodecParameters->extradata_size;
    stream.disposition = fill_disposition(pStream->disposition);

    // Add tags to stream.
    const AVDictionaryEntry *tag = NULL;
    while ((
        tag = av_dict_get(pStream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      Tag t = {
          .key = tag->key,
          .value = tag->value,
      };
      stream.tags.push_back(t);
    }

    r.streams.push_back(stream);
  }

  // Loop through the chapters (if any).
  for (int i = 0; i < pFormatContext->nb_chapters; i++) {
    AVChapter *chapter = pFormatContext->chapters[i];

    // Format timebase string to buf.
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%d%s%d", chapter->time_base.num, (char *)"/",
               chapter->time_base.den);

    Chapter c = {
        .id = (int)chapter->id,
        .time_base = buf.str,
        .start = (float)chapter->start,
        .end = (float)chapter->end,
    };

    // Add tags to chapter.
    const AVDictionaryEntry *tag = NULL;
    while ((
        tag = av_dict_get(chapter->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      Tag t = {
          .key = tag->key,
          .value = tag->value,
      };
      c.tags.push_back(t);
    }

    r.chapters.push_back(c);
  }

  avformat_close_input(&pFormatContext);
  return r;
}

EMSCRIPTEN_BINDINGS(constants) {
  function("avformat_version", &c_avformat_version);
  function("avcodec_version", &c_avcodec_version);
  function("avutil_version", &c_avutil_version);
}

EMSCRIPTEN_BINDINGS(structs) {
  emscripten::value_object<Tag>("Tag")
      .field("key", &Tag::key)
      .field("value", &Tag::value);
  register_vector<Tag>("Tag");

  emscripten::value_object<ProbeRational>("ProbeRational")
      .field("num", &ProbeRational::num)
      .field("den", &ProbeRational::den);

  emscripten::value_object<Disposition>("Disposition")
      .field("default_flag", &Disposition::default_flag)
      .field("dub", &Disposition::dub)
      .field("original", &Disposition::original)
      .field("comment", &Disposition::comment)
      .field("lyrics", &Disposition::lyrics)
      .field("karaoke", &Disposition::karaoke)
      .field("forced", &Disposition::forced)
      .field("hearing_impaired", &Disposition::hearing_impaired)
      .field("visual_impaired", &Disposition::visual_impaired)
      .field("clean_effects", &Disposition::clean_effects)
      .field("attached_pic", &Disposition::attached_pic)
      .field("timed_thumbnails", &Disposition::timed_thumbnails)
      .field("captions", &Disposition::captions)
      .field("descriptions", &Disposition::descriptions)
      .field("metadata", &Disposition::metadata)
      .field("dependent", &Disposition::dependent)
      .field("still_image", &Disposition::still_image);

  emscripten::value_object<Stream>("Stream")
      .field("index", &Stream::index)
      .field("id", &Stream::id)
      .field("start_time", &Stream::start_time)
      .field("duration", &Stream::duration)
      .field("codec_type", &Stream::codec_type)
      .field("codec_name", &Stream::codec_name)
      .field("codec_long_name", &Stream::codec_long_name)
      .field("codec_tag_string", &Stream::codec_tag_string)
      .field("codec_tag", &Stream::codec_tag)
      .field("format", &Stream::format)
      .field("bit_rate", &Stream::bit_rate)
      .field("profile", &Stream::profile)
      .field("level", &Stream::level)
      .field("width", &Stream::width)
      .field("height", &Stream::height)
      .field("coded_width", &Stream::coded_width)
      .field("coded_height", &Stream::coded_height)
      .field("has_b_frames", &Stream::has_b_frames)
      .field("sample_aspect_ratio", &Stream::sample_aspect_ratio)
      .field("sample_aspect_ratio_rational",
             &Stream::sample_aspect_ratio_rational)
      .field("display_aspect_ratio", &Stream::display_aspect_ratio)
      .field("display_aspect_ratio_rational",
             &Stream::display_aspect_ratio_rational)
      .field("display_aspect_ratio_label", &Stream::display_aspect_ratio_label)
      .field("pix_fmt", &Stream::pix_fmt)
      .field("color_range", &Stream::color_range)
      .field("color_space", &Stream::color_space)
      .field("color_transfer", &Stream::color_transfer)
      .field("color_primaries", &Stream::color_primaries)
      .field("chroma_location", &Stream::chroma_location)
      .field("field_order", &Stream::field_order)
      .field("sample_fmt", &Stream::sample_fmt)
      .field("channels", &Stream::channels)
      .field("channel_layout", &Stream::channel_layout)
      .field("sample_rate", &Stream::sample_rate)
      .field("bits_per_sample", &Stream::bits_per_sample)
      .field("initial_padding", &Stream::initial_padding)
      .field("frame_size", &Stream::frame_size)
      .field("r_frame_rate", &Stream::r_frame_rate)
      .field("r_frame_rate_rational", &Stream::r_frame_rate_rational)
      .field("avg_frame_rate", &Stream::avg_frame_rate)
      .field("avg_frame_rate_rational", &Stream::avg_frame_rate_rational)
      .field("time_base", &Stream::time_base)
      .field("time_base_rational", &Stream::time_base_rational)
      .field("start_pts", &Stream::start_pts)
      .field("duration_ts", &Stream::duration_ts)
      .field("bits_per_raw_sample", &Stream::bits_per_raw_sample)
      .field("nb_frames", &Stream::nb_frames)
      .field("extradata_size", &Stream::extradata_size)
      .field("disposition", &Stream::disposition)
      .field("tags", &Stream::tags);
  register_vector<Stream>("Stream");

  emscripten::value_object<Chapter>("Chapter")
      .field("id", &Chapter::id)
      .field("time_base", &Chapter::time_base)
      .field("start", &Chapter::start)
      .field("end", &Chapter::end)
      .field("tags", &Chapter::tags);
  register_vector<Chapter>("Chapter");

  emscripten::value_object<FileInfoResponse>("FileInfoResponse")
      .field("name", &FileInfoResponse::name)
      .field("duration", &FileInfoResponse::duration)
      .field("bit_rate", &FileInfoResponse::bit_rate)
      .field("url", &FileInfoResponse::url)
      .field("nb_streams", &FileInfoResponse::nb_streams)
      .field("flags", &FileInfoResponse::flags)
      .field("streams", &FileInfoResponse::streams)
      .field("nb_chapters", &FileInfoResponse::nb_chapters)
      .field("chapters", &FileInfoResponse::chapters)
      .field("error", &FileInfoResponse::error);
  function("get_file_info", &get_file_info);
}
