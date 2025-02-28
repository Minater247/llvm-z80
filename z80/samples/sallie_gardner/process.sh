
set -e

mkdir -p cropped small bitmap

convert The_Horse_in_Motion-anim.gif horse_%03d.png

# Try to recenter more-or-less
convert horse_000.png -gravity center -crop 307x230-13+0 +repage cropped/horse_000.png
convert horse_001.png -gravity center -crop 307x230-10+0 +repage cropped/horse_001.png
convert horse_002.png -gravity center -crop 307x230-8+0 +repage cropped/horse_002.png
convert horse_003.png -gravity center -crop 307x230-4+0 +repage cropped/horse_003.png
convert horse_004.png -gravity center -crop 307x230-4+0 +repage cropped/horse_004.png
convert horse_005.png -gravity center -crop 307x230-3+0 +repage cropped/horse_005.png
convert horse_006.png -gravity center -crop 307x230-2+0 +repage cropped/horse_006.png
convert horse_007.png -gravity center -crop 307x230+2+0 +repage cropped/horse_007.png
convert horse_008.png -gravity center -crop 307x230-1+0 +repage cropped/horse_008.png
convert horse_009.png -gravity center -crop 307x230-10+0 +repage cropped/horse_009.png
convert horse_010.png -gravity center -crop 307x230-5+0 +repage cropped/horse_010.png

for i in *.png
do
  #convert cropped/$i -scale 128x96! small/$i
  convert cropped/$i -scale 256x192! small/$i
done

for i in *.png
do
  #convert small/$i -colorspace Gray -contrast-stretch 0 -threshold 70% bitmap/$i
  #convert small/$i -monochrome bitmap/$i
  convert small/$i -colorspace Gray -dither FloydSteinberg -monochrome -threshold 99% bitmap/$i
done

for i in bitmap/*.png
do
  n=$(basename $i | cut -f1 -d".")".zx.bin"
  python3 reorder_zx.py $i $n
done

for i in *.zx.bin
do
  python3 rle2.py encode $i $i.rle
done

python3 files2c.py horse_*.rle horse.h

