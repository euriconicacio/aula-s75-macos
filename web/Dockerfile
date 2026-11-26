# Serves the open-aula WebHID configurator (https://github.com/paulcoding810/open-aula)
# from localhost. open-aula has no license, so it is not copied into this repository:
# the build fetches it from upstream, pinned to a reviewed commit.
#
# The container only serves static files. The browser on the host talks to the
# keyboard through WebHID, so no USB access is passed into the container.

FROM alpine:3 AS fetch
ARG OPEN_AULA_REPO=https://github.com/paulcoding810/open-aula.git
ARG OPEN_AULA_COMMIT=1216cc8b85a98cac84a650545b5a69bfdeead498
RUN apk add --no-cache git \
 && git init /src \
 && git -C /src remote add origin "$OPEN_AULA_REPO" \
 && git -C /src fetch --depth 1 origin "$OPEN_AULA_COMMIT" \
 && git -C /src checkout --quiet FETCH_HEAD \
 && test "$(git -C /src rev-parse HEAD)" = "$OPEN_AULA_COMMIT"

FROM nginxinc/nginx-unprivileged:stable-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=fetch /src/web /usr/share/nginx/html
EXPOSE 8080
