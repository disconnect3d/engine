struct {
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} effect;
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} effect2;
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} screen;
	struct {
		int		width, height;
	} work;
	qboolean started;
} notfbo;


