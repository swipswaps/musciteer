class StreamBegin
  attr_reader :stream_id
  attr_reader :track
  attr_accessor :shown

  def initialize(attrs)
    attrs.each do |k,v|
      case k
      when "track"
        @track = Track.new(v)
      else
        instance_variable_set("@#{k}", v) unless v.nil?
      end
    end
    @shown = false
  end
end
