#include "ebu/list/st2110/pch.h"
#include "ebu/list/st2110/d20/video_format_detector.h"
#include "ebu/list/st2110/d20/packet.h"

using namespace ebu_list::st2110::d20;
using namespace ebu_list::st2110;
using namespace ebu_list;

//------------------------------------------------------------------------------
namespace
{
    constexpr auto maximum_packets_per_frame = 30000; // TODO: this is enough for UHD
    constexpr auto minimum_packets_per_frame = 100;

    std::tuple<int, int> get_dimensions_from_max_line(int max_line, bool is_field_based)
    {
        if (is_field_based)
        {
            switch (max_line)
            {
            case 539: return { 1920, 1080 };

            default:
                break;
            }
        }
        else
        {
            switch (max_line)
            {
            case 719: return { 1280, 720 };
            case 1079: return { 1920, 1080 };
            case 2159: return { 3840, 2160 }; // TODO: calculate width correctly

            default:
                break;
            }
        }

        // TODO: check this
        return { 0,0 };
    }
}

//------------------------------------------------------------------------------

detector::status packet_spacing_analyzer::handle_data(const rtp::packet& packet)
{
    if (status_ != detector::status::detecting) return status_;

    if (last_packet_timestamp_)
    {
        const auto delta = packet.info.udp.packet_time - *last_packet_timestamp_;

        if (last_frame_was_marked_)
        {
            ++frame_start_packet_count_;
            frame_start_packet_total_ += delta;
        }
        else
        {
            ++regular_packet_count_;
            regular_packet_total_ += delta;
        }
    }

    last_packet_timestamp_ = packet.info.udp.packet_time;
    last_frame_was_marked_ = packet.info.rtp().marker();

    constexpr auto minimum_frame_start_packet_count = 3;
    if (frame_start_packet_count_ == minimum_frame_start_packet_count)
    {
        status_ = detector::status::valid;
    }

    return status_;
}

st2110::d21::read_schedule packet_spacing_analyzer::get_schedule() const noexcept
{
    constexpr auto minimum_ratio = 10;
    const auto is_gapped = (average_frame_start_packet_spacing() > average_regular_packet_spacing() * minimum_ratio);
    return is_gapped ? st2110::d21::read_schedule::gapped : st2110::d21::read_schedule::linear;
}

clock::duration packet_spacing_analyzer::average_regular_packet_spacing() const noexcept
{
    if (regular_packet_count_ == 0) return {};
    return regular_packet_total_ / regular_packet_count_;
}

clock::duration packet_spacing_analyzer::average_frame_start_packet_spacing() const noexcept
{
    if (frame_start_packet_count_ == 0) return {};
    return frame_start_packet_total_ / frame_start_packet_count_;
}


//------------------------------------------------------------------------------

detector::status line_data_analyzer::handle_data(const rtp::packet& packet)
{
    auto& sdu = packet.sdu;

    constexpr auto minimum_size = sizeof(raw_extended_sequence_number) + sizeof(raw_line_header);
    if (sdu.view().size() < minimum_size)
    {
        return detector::status::invalid;
    }

    auto p = sdu.view().data();
    const auto end = sdu.view().data() + sdu.view().size();

    // skip esn
    p += sizeof(raw_extended_sequence_number);

    while (p < end)
    {
        const auto line_header = line_header_lens(*reinterpret_cast<const raw_line_header*>(p));
        p += sizeof(raw_line_header);

        max_line_number_ = std::max(max_line_number_, int(line_header.line_number()));

        is_field_based_ |= (line_header.field_identification() != 0);

        if (!line_header.continuation()) break;
    }

    return detector::status::detecting;
}

int line_data_analyzer::max_line_number() const noexcept
{
    return max_line_number_;
}

bool line_data_analyzer::is_field_based() const noexcept
{
    return is_field_based_;
}

//------------------------------------------------------------------------------

video_format_detector::video_format_detector()
    : detector_({ maximum_packets_per_frame, minimum_packets_per_frame })
{
}

detector::status video_format_detector::handle_data(const rtp::packet& packet)
{
    const auto la_result = line_analyzer_.handle_data(packet);
    if (la_result == detector::status::invalid) return detector::status::invalid;
    const auto sa_result = spacing_analyzer_.handle_data(packet);
    if (sa_result == detector::status::invalid) return detector::status::invalid;

    return detector_.handle_data(packet);
}

detector::details video_format_detector::get_details() const
{
    auto result = video_description{};
    result.packets_per_frame = detector_.packets_pre_frame();
    result.rate = detector_.rate();

    result.scan_type = (line_analyzer_.is_field_based() ? media::video::scan_type::INTERLACED : media::video::scan_type::PROGRESSIVE);

    const auto [width, height] = get_dimensions_from_max_line(line_analyzer_.max_line_number(), line_analyzer_.is_field_based());
    result.dimensions.width = static_cast<uint16_t>(width);
    result.dimensions.height = static_cast<uint16_t>(height);
    result.schedule = spacing_analyzer_.get_schedule();

    // TODO: set these via configuration
    result.color_depth = 10;
    result.sampling = media::video::video_sampling::YCbCr_4_2_2;
    result.colorimetry = media::video::colorimetry::BT709;

    return result;
}
