import pynmea2

sentence = "$GPGGA,092750.000,5321.6802,N,00630.3372,W,4,8,1.03,61.7,M,55.2,M,,*76"

try:
    msg = pynmea2.parse(sentence)
    print(f"type:          {type(msg)}")
    print(f"sentence_type: {msg.sentence_type!r}")
    print(f"talker:        {msg.talker!r}")
    print(f"gps_qual:      {msg.gps_qual!r}")
    print(f"latitude:      {msg.latitude!r}")
except Exception as e:
    print(f"ParseError: {e}")
