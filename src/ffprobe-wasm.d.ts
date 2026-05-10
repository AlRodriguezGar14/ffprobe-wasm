export interface EmbindVector<T> {
  size(): number
  get(index: number): T
}

export interface Tag {
  key: string
  value: string
}

export interface ProbeRational {
  num: number
  den: number
}

export interface Disposition {
  default_flag: number
  dub: number
  original: number
  comment: number
  lyrics: number
  karaoke: number
  forced: number
  hearing_impaired: number
  visual_impaired: number
  clean_effects: number
  attached_pic: number
  timed_thumbnails: number
  captions: number
  descriptions: number
  metadata: number
  dependent: number
  still_image: number
}

export interface Stream {
  index: number
  id: number
  start_time: number
  duration: number
  codec_type: string
  codec_name: string
  codec_long_name: string
  codec_tag_string: string
  codec_tag: string
  bit_rate: number
  profile: string
  level: number
  width: number
  height: number
  has_b_frames: number
  sample_aspect_ratio: string
  sample_aspect_ratio_rational: ProbeRational
  display_aspect_ratio: string
  display_aspect_ratio_rational: ProbeRational
  display_aspect_ratio_label: string
  pix_fmt: string
  color_range: string
  color_space: string
  color_transfer: string
  color_primaries: string
  chroma_location: string
  field_order: string
  sample_fmt: string
  channels: number
  channel_layout: string
  sample_rate: number
  bits_per_sample: number
  initial_padding: number
  frame_size: number
  r_frame_rate: string
  r_frame_rate_rational: ProbeRational
  avg_frame_rate: string
  avg_frame_rate_rational: ProbeRational
  time_base: string
  time_base_rational: ProbeRational
  start_pts: number
  duration_ts: number
  bits_per_raw_sample: number
  nb_frames: number
  extradata_size: number
  disposition: Disposition
  tags: EmbindVector<Tag>
}

export interface Chapter {
  id: number
  time_base: string
  start: number
  end: number
  tags: EmbindVector<Tag>
}

export interface FileInfoResponse {
  name: string
  duration: number
  bit_rate: number
  url: string
  nb_streams: number
  flags: number
  streams: EmbindVector<Stream>
  nb_chapters: number
  chapters: EmbindVector<Chapter>
  error: string
}

export interface FfprobeModule {
  avformat_version(): string
  avcodec_version(): string
  avutil_version(): string
  get_file_info(filename: string): FileInfoResponse
}
