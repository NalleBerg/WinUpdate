from PIL import Image, ImageDraw, ImageFont
import math

# Recreate the logo based on SVG description: blue sphere with Windows logo and arrow
svg_file = 'winupdate_logo.svg'
png_output = 'winupdate_logo.png'

size = 96
img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Blue sphere gradient (simplified as solid colors in rings)
center = size // 2
radius = int(size * 0.47)

# Outer sphere (darker blue)
draw.ellipse([center-radius, center-radius, center+radius, center+radius], 
             fill=(25, 118, 210, 255))

# Inner highlight (lighter blue) 
inner_r = int(radius * 0.7)
draw.ellipse([center-inner_r, center-inner_r-10, center+inner_r, center+inner_r-10],
             fill=(99, 170, 240, 200))

# Sphere border
draw.ellipse([center-radius, center-radius, center+radius, center+radius],
             outline=(45, 58, 74, 255), width=2)

# Windows logo (4 squares arranged slightly curved)
sq_size = int(size * 0.18)
spacing = int(size * 0.02)

# Position squares (slightly left and up from center)
base_x = center - sq_size - spacing//2 - 6
base_y = center - sq_size - spacing//2 - 5

# Top left square
draw.rectangle([base_x, base_y, base_x+sq_size, base_y+sq_size],
               fill=(25, 118, 210, 255))

# Top right square
draw.rectangle([base_x+sq_size+spacing, base_y-2, base_x+sq_size*2+spacing, base_y+sq_size-2],
               fill=(25, 118, 210, 255))

# Bottom left square
draw.rectangle([base_x+2, base_y+sq_size+spacing, base_x+sq_size+2, base_y+sq_size*2+spacing],
               fill=(25, 118, 210, 255))

# Bottom right square
draw.rectangle([base_x+sq_size+spacing+2, base_y+sq_size+spacing-2, 
                base_x+sq_size*2+spacing+2, base_y+sq_size*2+spacing-2],
               fill=(25, 118, 210, 255))

# Arrow (counterclockwise arrow at bottom - simplified as curved arrow symbol)
# Draw an arc with arrowhead
arrow_y = int(size * 0.7)
arrow_start_x = int(size * 0.3)
arrow_end_x = int(size * 0.7)

# Use a bold yellow/gold color for the arrow
arrow_color = (255, 215, 0, 255)

# Draw curved path (arc shape)
for i in range(-20, 21):
    x = center + i
    y = arrow_y + int(8 * math.sin(i * 0.15))
    if 0 <= x < size and 0 <= y < size:
        draw.ellipse([x-2, y-2, x+2, y+2], fill=arrow_color)

# Arrowhead (pointing left) - triangle
arrow_x = arrow_start_x
arrow_y_mid = arrow_y
draw.polygon([
    (arrow_x-6, arrow_y_mid),
    (arrow_x+2, arrow_y_mid-6),
    (arrow_x+2, arrow_y_mid+6)
], fill=arrow_color)

img.save(png_output, 'PNG')
print(f"Created {png_output}: {img.size}, mode: {img.mode}")
